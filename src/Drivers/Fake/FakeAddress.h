/* Copyright (c) 2019, Stanford University
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HOMA_DRIVERS_FAKE_FAKEADDRESS_H
#define HOMA_DRIVERS_FAKE_FAKEADDRESS_H

#include <Homa/Driver.h>

namespace Homa {
namespace Drivers {
namespace Fake {

/**
 * A container for an FakeNetwork network address.
 */
struct FakeAddress : public Driver::Address {
    explicit FakeAddress(const uint64_t addressId);
    explicit FakeAddress(const char* addressStr);
    explicit FakeAddress(const Raw* const raw);
    FakeAddress(const FakeAddress& other);
    std::string toString() const;
    void toRaw(Raw* raw) const;

    static uint64_t toAddressId(const char* addressStr);

    /// FakeAddress identifier
    uint64_t address;
};

}  // namespace Fake
}  // namespace Drivers
}  // namespace Homa

#endif  // HOMA_DRIVERS_FAKE_FAKEADDRESS_H
