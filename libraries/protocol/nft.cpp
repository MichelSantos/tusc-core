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
#include <graphene/protocol/nft.hpp>

#include <graphene/protocol/asset_ops.hpp>

#include <fc/io/raw.hpp>

namespace graphene {
   namespace protocol {
      void nft_series_create_operation::validate() const {
         const asset_id_type &core = asset_id_type();

         FC_ASSERT(fee.amount >= 0, "Fee amount should not be negative");
         FC_ASSERT(fee.asset_id == core, "The fee should be paid in core asset");
         FC_ASSERT(royalty_fee_centipercent >= 0, "Royalty fee percentage should not be negative");
         FC_ASSERT(royalty_fee_centipercent <= GRAPHENE_100_PERCENT,
                   "Royalty fee percentage should be not exceed GRAPHENE_100_PERCENT");
      }

      void nft_mint_operation::validate() const {
         const asset_id_type &core = asset_id_type();

         FC_ASSERT(fee.amount >= 0, "Fee amount should not be negative");
         FC_ASSERT(fee.asset_id == core, "The fee should be paid in core asset");

         FC_ASSERT(asset_id != core, "The token to mint may not be the core token");
         FC_ASSERT(subdivisions > 0, "The amount of subdivisions to mint should be positive");

         FC_ASSERT(min_price_per_subdivision.amount >= 0,
                   "The minimum price per subdivision should not be negative");
         FC_ASSERT(min_price_per_subdivision.asset_id == core,
                   "The minimum price per subdivision should be denominated in the core asset");

         FC_ASSERT(req_backing_per_subdivision.amount >= 0,
                   "The required backing per subdivision should not be negative");
         FC_ASSERT(req_backing_per_subdivision.asset_id == core,
                   "The required backing per subdivision should be denominated in the core asset");

         FC_ASSERT(req_backing_per_subdivision.amount <= min_price_per_subdivision.amount,
                   "The required backing per subdivision should not exceed the minimum price per subdivision");
      }
   }
}

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::nft_series_create_operation::fee_parameters_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::nft_series_create_operation )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::nft_mint_operation::fee_parameters_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::nft_mint_operation )
