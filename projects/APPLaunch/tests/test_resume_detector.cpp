/*
 * SPDX-License-Identifier: MIT
 *
 * ResumeDetector: only a jump of (boottime - monotonic), a real suspend, is reported.
 */

#include "../main/ui/model/resume_detector.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

int main()
{
    {   // the first sample is a baseline, whatever the offset (a long uptime with earlier sleeps)
        ResumeDetector d;
        CHECK(d.sample(900000, 100000) == 0);
        CHECK(d.sample(900500, 100500) == 0);
    }
    {   // ordinary 500 ms ticks with a few ms of jitter between the two reads, never a report
        ResumeDetector d;
        int64_t mono = 10000;
        CHECK(d.sample(mono, mono) == 0);
        const int jitter[] = {0, 1, -1, 3, -2, 5, 0, 4, -4, 2};
        for (int i = 0; i < 400; ++i) {
            mono += 500 + (i % 7) - 3;
            CHECK(d.sample(mono + jitter[i % 10], mono) == 0);
        }
    }
    {   // a stalled UI thread (monotonic and boottime advance together by 30 s) is not a sleep
        ResumeDetector d;
        CHECK(d.sample(1000, 1000) == 0);
        CHECK(d.sample(31000, 31000) == 0);
    }
    {   // small sleeps below the threshold are ignored, at the threshold they count
        ResumeDetector d;
        CHECK(d.sample(5000, 5000) == 0);
        CHECK(d.sample(5500 + 1999, 5500) == 0);
        CHECK(d.sample(6000 + 1999 + 2000, 6000) == 2000);
    }
    {   // a 5 s suspend is reported once, with its length; the next tick is quiet; a second suspend is reported again
        ResumeDetector d;
        CHECK(d.sample(1000, 1000) == 0);
        CHECK(d.sample(1500, 1500) == 0);
        CHECK(d.sample(7000, 2000) == 5000);       // 5 s in S3: boottime moved 5.5 s, monotonic 0.5 s
        CHECK(d.sample(7500, 2500) == 0);
        CHECK(d.sample(8000, 3000) == 0);
        CHECK(d.sample(3500 + 5000 + 6700, 3500) == 6700);
        CHECK(d.sample(4000 + 5000 + 6700, 4000) == 0);
    }
    {   // two suspends in a row with no tick between add up in one report
        ResumeDetector d;
        CHECK(d.sample(0, 0) == 0);
        CHECK(d.sample(500 + 4000 + 3000, 500) == 7000);
    }
    {   // an offset that goes backwards (clock reset) or stays equal reports nothing and re-bases
        ResumeDetector d;
        CHECK(d.sample(50000, 1000) == 0);
        CHECK(d.sample(1500, 1500) == 0);          // offset 49000 -> 0
        CHECK(d.sample(2000, 2000) == 0);          // equal
        CHECK(d.sample(2500 + 2500, 2500) == 2500); // measured against the new base
        CHECK(d.sample(0, 5000) == 0);             // negative offset
        CHECK(d.sample(500, 5500) == 0);
    }
    std::puts("resume_detector OK");
    return 0;
}
