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

      void_result nft_mint_evaluator::do_evaluate(const nft_mint_operation &op) {
         try {
            const graphene::chain::database& d = db();

            // Reject any activity before the hardfork time
            auto now = d.head_block_time();
            FC_ASSERT(HARDFORK_NFT_M1_PASSED(now), "NFT Minting is not yet enabled");

            // Verify the existence of the associated asset
            const asset_object &t = op.asset_id(d);
            token_to_associate = &t;

            // Verify that the issuer of the operation is also the issuer of the asset
            FC_ASSERT(op.issuer == t.issuer,
                      "Incorrect issuer for asset! (${op.issuer} != ${t.issuer})",
                      ("op.issuer", op.issuer)("t.issuer", t.issuer));

            // Verify that the token's supply is permitted to increase
            FC_ASSERT(t.can_create_new_supply(), "The asset to mint is prohibited from an increase in supply");

            // Verify that the token is not a liquidity pool asset
            FC_ASSERT(!t.is_liquidity_pool_share_asset(), "The asset to mint may not be a liquidity pool asset");

            // Verify that the token is not a liquidity pool asset
            FC_ASSERT(!t.is_market_issued(), "The asset to mint may not be a market-issued asset");

            // Verify that associated asset is a sub-asset (i.e. not a sub-asset)
            // Graphene convention stipulates that parent assets have no periods (".") in the name
            const std::size_t idx_period = t.symbol.find('.');
            FC_ASSERT(idx_period != std::string::npos,
                      "The asset associated with a minting should be a sub-asset (${asset_name})",
                      ("asset_name", t.symbol)
            );

            // Determine the parent asset name
            const std::string &parent_asset_name = t.symbol.substr(0, idx_period);

            // Verify the existence of the parent asset
            const auto& asset_idx = d.get_index_type<asset_index>().indices().get<by_symbol>();
            auto itr_parent = asset_idx.find(parent_asset_name);
            FC_ASSERT(itr_parent != asset_idx.end(),
                      "The parent asset could not be found for the associated asset (${asset_name})",
                      ("asset_name", t.symbol)
            );
            const asset_id_type &s_id = itr_parent->id;
            const asset_object &s = s_id(d);
            series_to_associate = &s;

            // Determine the currently expected maximum backing behind the token to mint
            const asset_dynamic_data_object &t_addo = t.dynamic_asset_data_id(d);
            token_dyn_data = &t_addo;

            const share_type& t_whole_token = asset::scaled_precision(t.precision).value;
            const share_type& t_max_supply = t_whole_token;
            const fc::uint128_t &proposed_new_supply = fc::uint128_t(t_addo.current_supply.value) + op.subdivisions.value;
            FC_ASSERT(proposed_new_supply <= t_max_supply,
                      "Minting more subdivisions than are possible in a single whole token is prohibited");
            FC_ASSERT(proposed_new_supply <= t_addo.current_max_supply,
                      "Minting more subdivisions than permitted by the token's maximum supply is prohibited");

            // Check supply quantities
            const auto& token_id_idx = d.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
            auto token_itr = token_id_idx.find(op.asset_id);
            const bool& will_be_first_minting = (token_itr == token_id_idx.end());
            if (will_be_first_minting) {
               // Guard against a newly minted asset that already has a supply
               // because the existing supply is not being tracked through the Inventory
               FC_ASSERT(t_addo.current_supply == 0,
                         "The first minting of an asset should have no supply already in existence");
            } else {
               // Check the existing supply
               const nft_token_object &t_obj = *token_itr;
               // The maximum supply of an NFT that is permitted to exist equals the 10^precision - amount_burned
               const share_type& max_supply = t_whole_token - t_obj.amount_burned;
               // The amount remaining to be minted equals the max_supply - amount_minted
               const share_type& remaining_capacity = max_supply - t_obj.amount_minted;
               FC_ASSERT(op.subdivisions <= remaining_capacity,
                         "The amount requested to mint (${amount_requested}) would violate the remaining capacity to exist (${remaining_capacity})",
                         ("amount_requested", op.subdivisions.value)
                         ("remaining_capacity", remaining_capacity.value)
               );
            }

            // During a secondary minting, prohibit the modification of either the required backing or the minimum price
            if (!will_be_first_minting) {
               const nft_token_object &t_obj = *token_itr;
               FC_ASSERT(op.req_backing_per_subdivision == t_obj.req_backing_per_subdivision,
                         "Changing the required backing of a previously minted token is prohibited");
               FC_ASSERT(op.min_price_per_subdivision == t_obj.min_price_per_subdivision,
                         "Changing the minimum price of a previously minted token is prohibited");
            }

            // Guard against the maximum potential backing behind the token exceeding the current supply of the core asset
            // Calculate the scalar product of:
            // - the maximum supply of the associated asset
            // - the required backing in core asset
            const asset_id_type &core_id = asset_id_type(0);
            const asset_object &core = core_id(d);
            const asset_dynamic_data_object &core_addo = core.dynamic_asset_data_id(d);
            const fc::uint128_t &planned_max_backing_by_core
               = fc::uint128_t(op.req_backing_per_subdivision.amount.value) * t_max_supply.value;
            FC_ASSERT(planned_max_backing_by_core <= core_addo.current_max_supply,
                      "The maximum core backing that could be expected is predicted to exceed the current maximum supply of the core asset: (${token_max_supply}) (${core_max_supply})",
                      ("token_max_supply", t_addo.current_max_supply.value)
                      ("core_max_supply", core_addo.current_max_supply)
            );

            // Guard against the maximum royalty payment exceeding the current supply of the core asset
            // Calculate the scalar product of:
            // - the maximum supply of the associated asset
            // - royalty fee percentage
            const auto &series_name_idx = d.get_index_type<nft_series_index>().indices().get<by_nft_series_name>();
            auto series_itr = series_name_idx.find(parent_asset_name);
            const nft_series_object &so = *series_itr;
            const fc::uint128_t &royalty_for_full_token = fc::uint128_t(t_max_supply.value) * so.royalty_fee_centipercent / GRAPHENE_100_PERCENT;
            FC_ASSERT(royalty_for_full_token <= core_addo.current_supply);

            return void_result();
         } FC_CAPTURE_AND_RETHROW((op))
      }

      object_id_type nft_mint_evaluator::do_apply(const nft_mint_operation &op) {
         try {
            graphene::chain::database &d = db();

            const share_type &addition = op.subdivisions;

            const auto& token_id_idx = d.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
            auto token_itr = token_id_idx.find(op.asset_id);
            const bool& will_be_first_minting = (token_itr == token_id_idx.end());

            const nft_token_object* ptr_t_obj = nullptr;
            if (will_be_first_minting) {
               // Create a new internal object
               const string &token_name = token_to_associate->symbol;
               const asset_id_type &series_id = series_to_associate->id;
               const nft_token_object &obj = d.create<nft_token_object>(
                  [&d, &op, &token_name, &series_id](nft_token_object &_obj) {
                     _obj.token_id = op.asset_id;
                     _obj.token_name = token_name;
                     _obj.series_id = series_id;
                     _obj.amount_minted = op.subdivisions;
                     _obj.amount_in_inventory = op.subdivisions;
                     _obj.min_price_per_subdivision = op.min_price_per_subdivision;
                     _obj.req_backing_per_subdivision = op.req_backing_per_subdivision;
                     _obj.current_backing = asset(0, asset_id_type());
                  });
               ptr_t_obj = &obj;

            } else {
               // Modify the existing internal object
               const nft_token_object &obj = *token_itr;
               db().modify(obj, [&addition](nft_token_object &data) {
                  data.amount_minted += addition;
                  data.amount_in_inventory += addition;
               });
               ptr_t_obj = &obj;
            }

            // Modify the current supply of the token
            db().modify(*token_dyn_data, [&addition](asset_dynamic_data_object &data) {
               data.current_supply += addition;
            });

            return ptr_t_obj->id;
         } FC_CAPTURE_AND_RETHROW((op))
      }

   } // namespace chain
} // namespace graphene
