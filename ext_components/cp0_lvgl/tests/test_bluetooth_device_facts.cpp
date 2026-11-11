/*
 * SPDX-License-Identifier: MIT
 *
 * The BlueZ device fact helpers (cp0_bluetooth_device_facts.hpp): UUID shortening and the bounded UUID list the
 * backend puts in cp0_bt_device_t, with the real values of the Jabra Evolve 65 seen on the HackBerry.
 */
#include "cp0/cp0_bluetooth_device_facts.hpp"

#include <cassert>
#include <cstdio>

int main()
{
    using namespace cp0::bluetooth;
    assert(short_uuid("0000110B-0000-1000-8000-00805F9B34FB") == "110b");
    assert(short_uuid("00001200-0000-1000-8000-00805f9b34fb") == "1200");
    assert(short_uuid("") == "");
    // not a Bluetooth-base UUID: kept whole, lowercased
    assert(short_uuid("8EBBBF6B-1AF5-4F2F-8F0A-9A0C5A3B7E11") == "8ebbbf6b-1af5-4f2f-8f0a-9a0c5a3b7e11");
    // the same 16-bit value with another base is not shortened
    assert(short_uuid("0000110b-0000-1000-8000-00805f9b34fc") == "0000110b-0000-1000-8000-00805f9b34fc");
    assert(short_uuid("110b") == "110b");

    // Jabra Evolve 65 as BlueZ lists it
    const std::vector<std::string> evolve = {
        "00001101-0000-1000-8000-00805f9b34fb", "00001108-0000-1000-8000-00805f9b34fb",
        "0000110b-0000-1000-8000-00805f9b34fb", "0000110c-0000-1000-8000-00805f9b34fb",
        "0000110d-0000-1000-8000-00805f9b34fb", "0000110e-0000-1000-8000-00805f9b34fb",
        "0000111e-0000-1000-8000-00805f9b34fb", "00001200-0000-1000-8000-00805f9b34fb"};
    assert(join_uuids(evolve, 256) == "1101,1108,110b,110c,110d,110e,111e,1200");
    // duplicates dropped, order kept
    assert(join_uuids({"110b", "0000110B-0000-1000-8000-00805f9b34fb", "111e"}, 256) == "110b,111e");
    // bounded: whole entries only, never a cut one, result shorter than the buffer
    assert(join_uuids(evolve, 12) == "1101,1108");
    assert(join_uuids(evolve, 11) == "1101,1108");
    assert(join_uuids(evolve, 11).size() < 11);
    assert(join_uuids(evolve, 5) == "1101");
    assert(join_uuids(evolve, 4) == "");
    assert(join_uuids(evolve, 0) == "");
    assert(join_uuids({}, 256) == "");

    assert(is_a2dp_uuid("0000110b-0000-1000-8000-00805f9b34fb"));
    assert(is_a2dp_uuid("110a"));
    assert(!is_a2dp_uuid("111e"));
    std::puts("cp0_bluetooth_device_facts: ok");
    return 0;
}
