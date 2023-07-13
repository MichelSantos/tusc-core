/*
 * Copyright (c) 2023 Michel Santos and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <graphene/chain/nft_object.hpp>

using namespace graphene::chain;

// Derive the amount in circulation from the definition
// amount_minted = amount_in_inventory + amount_in_circulation + amount_burned
// Therefore
// amount_in_circulation = amount_minted - amount_in_inventory - amount_burned
share_type nft_token_object::amount_in_circulation() const {
   return amount_minted - amount_in_inventory - amount_burned;
}

bool nft_token_object::is_backable() const {
   return req_backing_per_subdivision.amount > 0;
}

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::nft_series_object )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::nft_token_object )
