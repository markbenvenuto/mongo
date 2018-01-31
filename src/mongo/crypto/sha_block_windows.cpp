/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include <initializer_list>

#include "mongo/crypto/sha1_block.h"
#include "mongo/crypto/sha256_block.h"

#include "mongo/config.h"
#include "mongo/util/assert_util.h"

namespace mongo {

namespace {

class BCryptHashLoader {
public:
    BCryptHashLoader() {
        loadAlgo(&algoSHA1, BCRYPT_SHA1_ALGORITHM, false);
        loadAlgo(&algoSHA256, BCRYPT_SHA256_ALGORITHM, false);

        loadAlgo(&algoSHA1Hmac, BCRYPT_SHA1_ALGORITHM, true);
        loadAlgo(&algoSHA256Hmac, BCRYPT_SHA256_ALGORITHM, true);
    }

    BCRYPT_ALG_HANDLE algoSHA256;
    BCRYPT_ALG_HANDLE algoSHA1;
    BCRYPT_ALG_HANDLE algoSHA256Hmac;
    BCRYPT_ALG_HANDLE algoSHA1Hmac;

private:
    void loadAlgo(BCRYPT_ALG_HANDLE* algo, const wchar_t* name, bool isHmac) {
        invariant(BCryptOpenAlgorithmProvider(
                      algo, name, NULL, isHmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0) ==
                  STATUS_SUCCESS);
    }
} hashLoader;

/**
 * Computes a SHA hash of 'input'.
 */
template <typename HashType>
HashType computeHashImpl(BCRYPT_ALG_HANDLE algo, std::initializer_list<ConstDataRange> input) {
    HashType output;

    BCRYPT_HASH_HANDLE hHash;

    fassert(50664,
            BCryptCreateHash(algo, &hHash, NULL, 0, NULL, 0, 0) == STATUS_SUCCESS &&

                std::all_of(begin(input),
                            end(input),
                            [&](const auto& i) {
                                return BCryptHashData(
                                           hHash,
                                           reinterpret_cast<PUCHAR>(const_cast<char*>(i.data())),
                                           i.length(),
                                           0) == STATUS_SUCCESS;
                            }) &&

                BCryptFinishHash(hHash, output.data(), output.size(), 0) == STATUS_SUCCESS &&

                BCryptDestroyHash(hHash) == STATUS_SUCCESS);

    return output;
}

/*
 * Computes a HMAC SHA'd keyed hash of 'input' using the key 'key', writes output into 'output'.
 */
template <typename HashType>
void computeHmacImpl(BCRYPT_ALG_HANDLE algo,
                     const uint8_t* key,
                     size_t keyLen,
                     const uint8_t* input,
                     size_t inputLen,
                     HashType* const output) {
    invariant(key && input);

    BCRYPT_HASH_HANDLE hHash;

    fassert(50665,
            BCryptCreateHash(algo, &hHash, NULL, 0, const_cast<PUCHAR>(key), keyLen, 0) ==
                    STATUS_SUCCESS &&

                BCryptHashData(hHash, const_cast<uint8_t*>(input), inputLen, 0) == STATUS_SUCCESS &&

                BCryptFinishHash(hHash, output->data(), output->size(), 0) == STATUS_SUCCESS &&

                BCryptDestroyHash(hHash) == STATUS_SUCCESS);
}

}  // namespace

SHA1BlockTraits::HashType SHA1BlockTraits::computeHash(
    std::initializer_list<ConstDataRange> input) {
    return computeHashImpl<SHA1BlockTraits::HashType>(hashLoader.algoSHA1, input);
}

SHA256BlockTraits::HashType SHA256BlockTraits::computeHash(
    std::initializer_list<ConstDataRange> input) {
    return computeHashImpl<SHA256BlockTraits::HashType>(hashLoader.algoSHA256, input);
}

void SHA1BlockTraits::computeHmac(const uint8_t* key,
                                  size_t keyLen,
                                  const uint8_t* input,
                                  size_t inputLen,
                                  HashType* const output) {
    return computeHmacImpl<HashType>(hashLoader.algoSHA1Hmac, key, keyLen, input, inputLen, output);
}

void SHA256BlockTraits::computeHmac(const uint8_t* key,
                                    size_t keyLen,
                                    const uint8_t* input,
                                    size_t inputLen,
                                    HashType* const output) {
    return computeHmacImpl<HashType>(
        hashLoader.algoSHA256Hmac, key, keyLen, input, inputLen, output);
}

}  // namespace mongo
