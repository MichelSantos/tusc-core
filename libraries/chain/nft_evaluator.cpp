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
#include <graphene/chain/database.hpp>
#include <graphene/protocol/nft.hpp>
#include <graphene/chain/nft_object.hpp>
#include <graphene/chain/nft_evaluator.hpp>

namespace graphene {
   namespace chain {
      void_result nft_series_create_evaluator::do_evaluate(const nft_series_create_operation &op) {
         try {
            const graphene::chain::database& d = db();

            // Reject any activity before the hardfork time
            auto now = d.head_block_time();
            FC_ASSERT(HARDFORK_NFT_M1_PASSED(now), "NFT Series Creation are not yet enabled");

            // Verify the royalty fee percentage
            FC_ASSERT(0 <= op.royalty_fee_centipercent,
                      "NFT Series Royalty Fee Percentage should not be negative");
            FC_ASSERT(op.royalty_fee_centipercent <= GRAPHENE_100_PERCENT,
                      "NFT Series Royalty Fee Percentage should not exceed GRAPHENE_100_PERCENT");

            // Verify the existence of the beneficiary account
            FC_ASSERT(d.find_object(op.beneficiary),
                      "The beneficiary account is unrecognized");

            // Verify the existence of the manager account
            if (op.manager.valid()) {
               FC_ASSERT(d.find_object(*op.manager),
                         "The manager account is unrecognized");
            }

            // Verify the existence of the associated asset
            const asset_object &a = op.asset_id(d);
            asset_to_associate = &a;

            // Verify that the issuer of the operation is also the issuer of the asset
            FC_ASSERT(op.issuer == a.issuer,
                      "Incorrect issuer for asset! (${op.issuer} != ${a.issuer})",
                      ("op.issuer", op.issuer)("a.issuer", a.issuer));

            // Verify that associated asset is a parent asset (i.e. not a sub-asset)
            // Graphene convention stipulates that parent assets have no periods (".") in the name
            FC_ASSERT(a.symbol.find('.') == std::string::npos,
                      "The asset associated with a series creation should not be a sub-asset (${asset_name})",
                      ("asset_name", a.symbol)
            );

            // Verify the asset does not have any sub-assets based on name
            // Example: Sub-assets of a series name "SERIESA" would have sub-assets like "SERIESA.SUB1", "SERIESA.SUB2"
            const auto& asset_idx = d.get_index_type<asset_index>().indices().get<by_symbol>();
            auto asset_pos = asset_idx.find(a.symbol);
            auto next_pos = ++asset_pos;
            if (next_pos != asset_idx.end()) {
               const string &next_asset_name = next_pos->symbol;
               const string &sub_asset_name_prefix = a.symbol + ".";
               if (next_asset_name.find(sub_asset_name_prefix) != std::string::npos) {
                  // A sub-asset was found
                  FC_THROW("A series may not be associated with an asset that already contains sub-assets");
               }
            }

            // Verify that the asset is not already associated with a series
            const auto &series_idx = d.get_index_type<nft_series_index>().indices().get<by_nft_series_asset_id>();
            auto series_itr = series_idx.find(op.asset_id);
            FC_ASSERT(series_itr == series_idx.end(),
                      "The asset (${asset_id}) is already a series with another series (${series_name})",
                      ("asset_id", op.asset_id)("series_name", series_itr->series_name)
            );

            return void_result();
         } FC_CAPTURE_AND_RETHROW((op))
      }

      object_id_type nft_series_create_evaluator::do_apply(const nft_series_create_operation &op) {
         try {
            graphene::chain::database &d = db();

            // TODO [Milestone 4]: Create the associated royalty series asset
            // TODO [Milestone 4]: Distribute the associated royalty series to the Issuer

            const string& asset_name = asset_to_associate->symbol;
            const nft_series_object &obj = d.create<nft_series_object>(
               [&d, &op, &asset_name](nft_series_object &_obj) {
                  _obj.asset_id = op.asset_id;
                  _obj.series_name = asset_name;
                  _obj.royalty_fee_centipercent = op.royalty_fee_centipercent;
                  _obj.beneficiary = op.beneficiary;
                  if (op.manager.valid()) {
                     _obj.manager = *op.manager;
                  } else {
                     // Default to the issuer if a manager is not specified
                     _obj.manager = op.issuer;
                  }
               });
            return obj.id;

         } FC_CAPTURE_AND_RETHROW((op))
      }
   } // namespace chain
} // namespace graphene
