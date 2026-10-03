/*
 * Settings > Apps backend access (see apps_backend.hpp).
 */

#include "apps_backend.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace apps_backend {

namespace {

constexpr const char *kDefaultBackend = "/usr/share/APPLaunch/bin/M5CardputerZero-AppStore";
constexpr size_t kOutputLimit = 4u << 20; // a catalogue is a few hundred KB at most

std::vector<std::string> split_tab(const std::string &line)
{
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == '\t') {
            fields.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string field(const std::vector<std::string> &values, size_t index)
{
    return index < values.size() ? values[index] : std::string();
}

// The backend writes its log lines (timestamps, "INFO ...") to the same stream as its records.
bool is_record(const std::string &line, const char *tag)
{
    const size_t n = std::strlen(tag);
    return line.size() > n && line.compare(0, n, tag) == 0 && line[n] == '\t';
}

} // namespace

std::string backend_path()
{
    const char *env = std::getenv("APPLAUNCH_APPSTORE_BIN");
    return env && env[0] ? env : kDefaultBackend;
}

bool available()
{
    return ::access(backend_path().c_str(), X_OK) == 0;
}

Result run(const std::vector<std::string> &args, int timeout_s)
{
    Result result;
    const std::string binary = backend_path();
    int pipe_fd[2];
    if (::pipe2(pipe_fd, O_CLOEXEC) != 0) return result;

    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(binary.c_str()));
    for (const std::string &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return result;
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        ::dup2(pipe_fd[1], STDOUT_FILENO);
        ::dup2(pipe_fd[1], STDERR_FILENO);
        const int null_fd = ::open("/dev/null", O_RDONLY);
        if (null_fd >= 0) ::dup2(null_fd, STDIN_FILENO);
        ::execv(binary.c_str(), argv.data());
        ::_exit(127);
    }
    ::close(pipe_fd[1]);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    bool timed_out = false;
    char buffer[4096];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            timed_out = true;
            break;
        }
        pollfd p{pipe_fd[0], POLLIN, 0};
        const int ready = ::poll(&p, 1, static_cast<int>(std::min<long long>(left, 500)));
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        const ssize_t got = ::read(pipe_fd[0], buffer, sizeof(buffer));
        if (got <= 0) break;
        if (result.out.size() < kOutputLimit) result.out.append(buffer, static_cast<size_t>(got));
    }
    ::close(pipe_fd[0]);
    if (timed_out) ::kill(-pid, SIGTERM);

    int status = 0;
    for (int i = 0; i < 100; ++i) { // up to 5 s for the child to go away
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0) { status = -1; break; }
        if (i == 50) ::kill(-pid, SIGKILL);
        ::usleep(50 * 1000);
    }
    result.rc = timed_out ? -2 : (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return result;
}

std::vector<App> parse_apps(const std::string &summary_output)
{
    std::vector<App> apps;
    std::istringstream lines(summary_output);
    std::string line;
    while (std::getline(lines, line)) {
        if (!is_record(line, "APP")) continue;
        const auto v = split_tab(line);
        App app;
        app.id = field(v, 1);
        app.title = field(v, 2);
        app.version = field(v, 3);
        app.category = field(v, 4);
        app.installed = field(v, 5) == "1";
        app.description = field(v, 8);
        app.author = field(v, 9);
        app.source = field(v, 14);
        app.installed_version = field(v, 18);
        app.package = field(v, 20);
        if (!app.id.empty() && !app.title.empty()) apps.push_back(std::move(app));
    }
    std::stable_sort(apps.begin(), apps.end(), [](const App &a, const App &b) {
        return std::lexicographical_compare(a.title.begin(), a.title.end(), b.title.begin(), b.title.end(),
                                            [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
    });
    return apps;
}

std::vector<Source> parse_sources(const std::string &registries_output)
{
    std::vector<Source> sources;
    std::istringstream lines(registries_output);
    std::string line;
    while (std::getline(lines, line)) {
        if (!is_record(line, "REG")) continue;
        const auto v = split_tab(line);
        Source source;
        source.url = field(v, 1);
        source.status = field(v, 2);
        source.apps = std::atoi(field(v, 3).c_str());
        source.enabled = field(v, 6) != "0";
        source.name = field(v, 7);
        source.builtin = field(v, 8) == "1";
        if (source.name.empty()) source.name = source.url;
        if (!source.url.empty()) sources.push_back(std::move(source));
    }
    return sources;
}

bool normalize_source(const std::string &input, std::string &url, std::string &name, std::string &error)
{
    std::string text;
    for (char c : input)
        if (!std::isspace(static_cast<unsigned char>(c))) text.push_back(c);
    if (text.empty()) {
        error = "Enter owner/repo";
        return false;
    }
    for (unsigned char c : text) {
        if (!(std::isalnum(c) || std::strchr("-._~:/?=&%@+", c))) {
            error = "Invalid character in the address";
            return false;
        }
    }
    auto strip_prefix = [&](const char *prefix) {
        const size_t n = std::strlen(prefix);
        if (text.compare(0, n, prefix) == 0) text.erase(0, n);
    };

    // raw.githubusercontent.com/<owner>/<repo>/<branch>/registry.json: keep as is, name it owner/repo.
    if (text.rfind("https://raw.githubusercontent.com/", 0) == 0) {
        url = text;
        std::vector<std::string> pieces;
        std::stringstream raw(text.substr(34));
        std::string piece;
        while (std::getline(raw, piece, '/'))
            if (!piece.empty()) pieces.push_back(piece);
        name = pieces.size() >= 2 ? pieces[0] + "/" + pieces[1] : text;
        return true;
    }
    // Any other https address that is not a GitHub repository page: a direct registry.json.
    if (text.rfind("https://", 0) == 0 && text.find("github.com/") == std::string::npos) {
        url = text;
        const size_t host_end = text.find('/', 8);
        name = host_end == std::string::npos ? text.substr(8) : text.substr(8, host_end - 8);
        return true;
    }
    if (text.rfind("http://", 0) == 0) {
        error = "Use https://";
        return false;
    }

    strip_prefix("https://");
    strip_prefix("www.");
    strip_prefix("github.com/");
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, '/'))
        if (!part.empty()) parts.push_back(part);
    if (parts.size() < 2) {
        error = "Use owner/repo";
        return false;
    }
    std::string repo = parts[1];
    if (repo.size() > 4 && repo.compare(repo.size() - 4, 4, ".git") == 0) repo.erase(repo.size() - 4);
    std::string branch = "main";
    if (parts.size() >= 4 && (parts[2] == "tree" || parts[2] == "blob")) branch = parts[3];
    url = "https://raw.githubusercontent.com/" + parts[0] + "/" + repo + "/" + branch + "/registry.json";
    name = parts[0] + "/" + repo;
    return true;
}

bool parse_package_job(const std::string &prepare_output, PackageJob &job)
{
    std::istringstream lines(prepare_output);
    std::string line;
    while (std::getline(lines, line)) {
        if (!is_record(line, "PACKAGE_JOB")) continue;
        const auto v = split_tab(line);
        if (v.size() < 7) return false;
        job = PackageJob();
        job.action = v[1];
        job.value = v[2];
        job.reinstall = v[3] == "1";
        job.desktop = v[4];
        job.transaction = v[5];
        job.pending_path = v[6];
        for (size_t i = 7; i < v.size(); ++i)
            if (!v[i].empty()) job.executables.push_back(v[i]);
        return !job.action.empty() && !job.value.empty() && !job.transaction.empty();
    }
    return false;
}

std::vector<std::string> privileged_argv(const PackageJob &job)
{
    std::vector<std::string> argv = {backend_path(), "--package-helper", job.action, "--package-value", job.value};
    if (job.reinstall) argv.push_back("--package-reinstall");
    if (!job.desktop.empty()) {
        argv.push_back("--package-desktop");
        argv.push_back(job.desktop);
    }
    argv.push_back("--package-transaction");
    argv.push_back(job.transaction);
    argv.push_back("--package-pending-path");
    argv.push_back(job.pending_path);
    for (const std::string &exec : job.executables) {
        argv.push_back("--package-exec");
        argv.push_back(exec);
    }
    return argv;
}

std::string error_text(const std::string &output, const std::string &fallback)
{
    std::string found;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (!is_record(line, "ERROR")) continue;
        const auto v = split_tab(line);
        if (v.size() > 1 && !v[1].empty()) found = v[1];
    }
    return found.empty() ? fallback : found;
}

} // namespace apps_backend
