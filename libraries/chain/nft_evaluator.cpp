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
#include <graphene/chain/is_authorized_asset.hpp>

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
            _asset_to_associate = &a;

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

            // The minimum sub-asset addendum, by Graphene convention, involves a period "." and one character.
            // Therefore, the minimum addition to the name of any potential sub-asset is two
            const uint8_t MIN_SUB_ASSET_NAME_ADDENDUM = 2;
            FC_ASSERT(a.symbol.size() <= GRAPHENE_MAX_ASSET_SYMBOL_LENGTH - MIN_SUB_ASSET_NAME_ADDENDUM,
               "Series name is too long to permit the creation of sub-assets");

            // Verify that the asset has a supply of NFT_ROYALTY_CLAIMS_COUNT subdivisions
            const asset_dynamic_data_object &a_addo = a.dynamic_asset_data_id(d);
            const share_type required_balance = NFT_ROYALTY_CLAIMS_COUNT;
            FC_ASSERT(a_addo.current_supply == required_balance,
                      "The asset should have a supply of ${required} subdivisions",
                      ("required", required_balance)
            );
            FC_ASSERT(a_addo.current_max_supply == required_balance,
                      "The asset should have a maximum supply of ${required} subdivisions",
                      ("required", required_balance)
            );
            FC_ASSERT(a.precision == 0,
                      "The asset should have a precision of zero rather than ${actual_precision}",
                      ("actual_precision", a.precision)
            );

            // Verify that the supply of NFT_ROYALTY_CLAIMS_COUNT subdivisions is held by the prospective creator
            const asset& creator_balance = d.get_balance(a.issuer, a.id);
            FC_ASSERT(creator_balance.amount == required_balance,
                      "The supply of the asset should be equal ${required} subdivisions and be entirely held by the asset issuer.  The asset issuer's balance equals ${available} subdivisions.",
                      ("required", required_balance)
                      ("available", creator_balance.amount)
            );

            // Verify that the asset's maximum supply is prohibited from increasing
            FC_ASSERT((a.options.issuer_permissions & lock_max_supply), "The asset should NOT be configured to update the max supply");
            FC_ASSERT((a.options.flags & lock_max_supply), "The asset should NOT be configured to update the max supply");

            // Verify that the asset CANNOT charge a market fee
            FC_ASSERT(!(a.options.issuer_permissions & charge_market_fee), "The asset should NOT be configured to charge a market fee");
            FC_ASSERT(!(a.options.flags & charge_market_fee), "The asset should NOT be configured to charge a market fee");

            // Verify that the asset is not already associated with a series
            const auto &series_idx = d.get_index_type<nft_series_index>().indices().get<by_nft_series_asset_id>();
            auto series_itr = series_idx.find(op.asset_id);
            FC_ASSERT(series_itr == series_idx.end(),
                      "The asset (${asset_id}) is already a series with another series (${series_name})",
                      ("asset_id", op.asset_id)("series_name", series_itr->series_name)
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

            return void_result();
         } FC_CAPTURE_AND_RETHROW((op))
      }

      object_id_type nft_series_create_evaluator::do_apply(const nft_series_create_operation &op) {
         try {
            graphene::chain::database &d = db();
            const string& asset_name = _asset_to_associate->symbol;

            // Create the NFT Series
            // Mimic asset_create_evaluator::do_apply()
            const account_id_type series_creator = op.issuer; // Previously verified to be NFT Series Issuer in do_evaluate()
            const nft_series_object &obj = d.create<nft_series_object>(
               [&d, &op, &asset_name, &series_creator](nft_series_object &_obj) {
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

                  // Track the initial royalty claimants
                  const share_type &initial_max_supply = NFT_ROYALTY_CLAIMS_COUNT;
                  _obj.royalty_claims[series_creator] = initial_max_supply;
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

            // Verify that the token's supply is permitted to increase
            FC_ASSERT(t.can_create_new_supply(), "The asset to mint is prohibited from an increase in supply");

            // Verify that the token is not a liquidity pool asset
            FC_ASSERT(!t.is_liquidity_pool_share_asset(), "The asset to mint may not be a liquidity pool asset");

            // Verify that the token is not a market-issued asset
            FC_ASSERT(!t.is_market_issued(), "The asset to mint may not be a market-issued asset");

            // Verify that associated asset is a sub-asset (i.e. not a parent asset)
            // Graphene convention stipulates that sub-assets have periods (".") in the name
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
            const asset_id_type &p_id = itr_parent->id;
            const asset_object &p = p_id(d);
            series_to_associate = &p;

            // Verify that the issuer of the operation is also the issuer of the parent Series
            FC_ASSERT(op.issuer == p.issuer,
                      "Minting may only be initiated by the Series Issuer! (${op.issuer} != ${series_issuer})",
                      ("op.issuer", op.issuer)("series_issuer", p.issuer));

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
            FC_ASSERT(series_itr != series_name_idx.end());
            const nft_series_object &so = *series_itr;
            // Perform an additional sanity check associating the Series with the parent asset
            FC_ASSERT(so.asset_id == p.id,
                      "The Series asset ID (${series_asset}) should match the parent asset to be minted (${parent_asset})",
                      ("series_asset", so.asset_id)
                      ("parent_asset", p.id)
            );
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
                     _obj.current_backing = asset(0, op.req_backing_per_subdivision.asset_id);
                     _obj.royalty_reservoir = asset(0, op.min_price_per_subdivision.asset_id);
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

      void_result nft_primary_transfer_evaluator::do_evaluate(const nft_primary_transfer_operation &op) {
         try {
            const graphene::chain::database& d = db();

            // Reject any activity before the hardfork time
            auto now = d.head_block_time();
            FC_ASSERT(HARDFORK_NFT_M1_PASSED(now), "NFT Primary Transfer is not yet enabled");

            const account_object& to_account = op.to(d);
            const asset_object& asset_type = op.amount.asset_id(d);

            // Verify that the asset is a minted token object
            const auto& token_id_idx = d.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
            auto token_itr = token_id_idx.find(op.amount.asset_id);
            const bool& is_nft = (token_itr != token_id_idx.end());
            FC_ASSERT(is_nft, "Primary transfers may only be performed for NFT tokens");
            const nft_token_object &nft_obj = *token_itr;
            ptr_token_obj = &nft_obj;

            const auto &series_idx = d.get_index_type<nft_series_index>().indices().get<by_nft_series_asset_id>();
            auto series_itr = series_idx.find(nft_obj.series_id);
            FC_ASSERT(series_itr != series_idx.end());
            const nft_series_object &series_obj = *series_itr;

            // Verify that the specified manager is actually the Series Manager
            FC_ASSERT(op.manager == series_obj.manager,
                      "Primary transfers may only be initiated by the Series Manager. (Series Manager is ${series_mgr}.  Operation Manger is ${op_mgr}.)",
                      ("series_mgr", series_obj.manager)
                      ("op_mgr", op.manager)
            );

            // Verify conventional transfer restrictions: is the recipient authorized to transact with the asset
            GRAPHENE_ASSERT(is_authorized_asset(d, to_account, asset_type),
                            transfer_to_account_not_whitelisted,
                            "'to' account ${to} is not whitelisted for asset ${asset}",
                            ("to", op.to)
                            ("asset", op.amount.asset_id)
            );

            // Transfer restrictions require the Issuer to be either the sender or recipient
            // The Series Manager is designated by the Series Issuer.
            // Therefore any primary transfer implicitly involves the Series Issuer as a sender.
            // No additional verifications are required.

            // Verify that the Inventory has sufficient assets to satisfy the primary transfer
            FC_ASSERT(op.amount.asset_id == nft_obj.token_id); // Redundant check from earlier in the function
            FC_ASSERT(op.amount.amount > 0); // Redundant check from the operation's validate()
            FC_ASSERT(op.amount.amount <= nft_obj.amount_in_inventory,
                      "The amount of the primary transfer (${requested} subdivisions) exceeds the available balance in the Inventory (${available} subdivisions)",
                      ("requested", op.amount.amount)
                      ("available", nft_obj.amount_in_inventory)
            );

            // Verify backing requirements
            _is_backing_required = nft_obj.req_backing_per_subdivision.amount > 0;
            if (_is_backing_required) {
               // If the NFT requires backing, verify that a provisioner has been specified
               FC_ASSERT(op.provisioner.valid(), "A provisioner should be provided for tokens that require backing");

               // If the NFT requires backing, verify that provisioner has sufficient balance
               const account_id_type& provisioner_id = *op.provisioner;
               const asset& provisioner_balance = d.get_balance(provisioner_id, nft_obj.req_backing_per_subdivision.asset_id);
               const fc::uint128_t& required_backing_uint128
                  = fc::uint128_t(op.amount.amount.value) * nft_obj.req_backing_per_subdivision.amount.value;
               bool sufficient_balance = required_backing_uint128 <= provisioner_balance.amount;
               FC_ASSERT(sufficient_balance,
                          "Insufficient Balance ${p_bal} in the provisioner account (${p}): Unable to transfer '${total_transfer}' from Inventory to '${to}'",
                          ("p_bal", d.to_pretty_string(provisioner_balance))
                          ("p", provisioner_id)
                          ("total_transfer", d.to_pretty_string(op.amount))
                          ("to",op.to)
               );
               _required_backing = asset(static_cast<int64_t>(required_backing_uint128), nft_obj.req_backing_per_subdivision.asset_id);
            } else {
               // If the NFT DOES NOT require backing, reject the specification of a provisioner
               // to avoid any possible confusion by the entity constructing the operations
               // (which should be the Manager)
               FC_ASSERT(!op.provisioner.valid(), "A provisioner should NOT be provided for tokens that DO NOT require backing");
            }

            return void_result();
         } FC_CAPTURE_AND_RETHROW((op))
      }

      void_result nft_primary_transfer_evaluator::do_apply(const nft_primary_transfer_operation &op) {
         try {
            graphene::chain::database &d = db();

            const nft_token_object& token_obj = *ptr_token_obj;
            if (_is_backing_required) {
               // Decrease the provisioning account of the required backing
               d.adjust_balance(*op.provisioner, -_required_backing);
            }

            const bool& is_backing_required = _is_backing_required;
            const asset& required_backing = _required_backing;
            db().modify(token_obj, [&op,&is_backing_required,&required_backing](nft_token_object &obj) {
               // Increase the backing in the Inventory
               if (is_backing_required) {
                  obj.current_backing.amount += required_backing.amount;
               }

               // Decrease the token in the Inventory
               obj.amount_in_inventory += -op.amount.amount;
            });


            // Increase the balance of the recipient account
            d.adjust_balance(op.to, op.amount);

            return void_result();
         } FC_CAPTURE_AND_RETHROW((op))
      }

      void_result nft_return_evaluator::do_evaluate(const nft_return_operation &op) {
         try {
            const graphene::chain::database& d = db();

            // Reject any activity before the hardfork time
            auto now = d.head_block_time();
            FC_ASSERT(HARDFORK_NFT_M2_PASSED(now), "NFT Returns are not yet enabled");

            const account_object& bearer_account = op.bearer(d);
            const asset_object& asset_type = op.amount.asset_id(d);

            // Verify that the asset is a minted token object
            const auto& token_id_idx = d.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
            auto token_itr = token_id_idx.find(op.amount.asset_id);
            const bool& is_nft = (token_itr != token_id_idx.end());
            FC_ASSERT(is_nft, "Returns may only be performed for NFT tokens");
            const nft_token_object &nft_obj = *token_itr;
            _ptr_token_obj = &nft_obj;

            // Determine information about the series and its issuer
            const auto &series_idx = d.get_index_type<nft_series_index>().indices().get<by_nft_series_asset_id>();
            auto series_itr = series_idx.find(nft_obj.series_id);
            FC_ASSERT(series_itr != series_idx.end());
            const nft_series_object &series_obj = *series_itr;
            const asset_object& series_asset_obj = series_obj.asset_id(d);
            const account_object& series_issuer = series_asset_obj.issuer(d);

            // Verify conventional transfer restrictions: is the asset transfer restricted
            if( asset_type.is_transfer_restricted() ) {
               // Only the Series Issuer may return the token to the Inventory
               GRAPHENE_ASSERT(bearer_account.id == series_issuer.id,
                               transfer_restricted_transfer_asset,
                               "Asset ${asset} has transfer_restricted flag enabled",
                               ("asset", op.amount.asset_id)
               );
            }

            // Verify conventional transfer restrictions: is the bearer authorized to transact with the asset
            GRAPHENE_ASSERT(is_authorized_asset(d, bearer_account, asset_type),
                            transfer_from_account_not_whitelisted,
                            "Bearer ${bearer} is not whitelisted for asset ${asset}",
                            ("bearer", bearer_account)
                            ("asset", op.amount.asset_id)
            );

            // Verify that the Bearer has sufficient assets to satisfy the return
            FC_ASSERT(op.amount.asset_id == nft_obj.token_id); // Redundant check from earlier in the function
            FC_ASSERT(op.amount.amount > 0); // Redundant check from the operation's validate()
            const asset &bearer_balance = d.get_balance(op.bearer, op.amount.asset_id);
            const bool sufficient_balance = op.amount.amount <= bearer_balance.amount;
            FC_ASSERT(sufficient_balance,
                      "The amount of the return (${returning} subdivisions) exceeds the bearer's available balance of (${available} subdivisions)",
                      ("returning", op.amount.amount)
                      ("available", bearer_balance.amount)
            );

            // Extra consistency check
            // Verify whether the amount being returned is consistent with the amount expected to be in circulation
            const share_type amount_in_circulation = nft_obj.amount_in_circulation();
            FC_ASSERT(op.amount.amount <= amount_in_circulation,
                      "Inconsistency Warning: The amount of the return (${returning} subdivisions) exceeds the amount in circulation (${circulation} subdivisions)",
                      ("returning", op.amount.amount)
                         ("circulation", amount_in_circulation)
            );

            // Verify the Inventory has adequate backing requirements
            if (nft_obj.is_backable()) {
               // As the NFT is backable, verify that sufficient backing is present in the Inventory
               const fc::uint128_t &expected_redemption_uint128
                  = fc::uint128_t(op.amount.amount.value) * nft_obj.req_backing_per_subdivision.amount.value;
               FC_ASSERT(expected_redemption_uint128 <= nft_obj.current_backing.amount,
                         "The NFT's Inventory has insufficient backing to accommodate a redemption");
               _redemption_amount = asset(static_cast<int64_t>(expected_redemption_uint128),
                                          nft_obj.req_backing_per_subdivision.asset_id);
            }

            return void_result();
         } FC_CAPTURE_AND_RETHROW((op))
      }

      void_result nft_return_evaluator::do_apply(const nft_return_operation &op) {
         try {
            graphene::chain::database &d = db();

            const nft_token_object& token_obj = *_ptr_token_obj;

            // Withdraw the NFT from the bearer's account
            d.adjust_balance(op.bearer, -op.amount);

            // Update the NFT object's balances
            const bool& is_redemption = _redemption_amount.amount.value > 0;
            const share_type& expected_redemption_amount = _redemption_amount.amount.value;
            d.modify(token_obj, [&op,&is_redemption,&expected_redemption_amount](nft_token_object &obj) {
               // Increase the token amount in the Inventory
               obj.amount_in_inventory += op.amount.amount;

               // Decrease the backing in the Inventory **if** it is a redemption
               if (is_redemption) {
                  obj.current_backing.amount -= expected_redemption_amount;
               }
            });

            // Increase the bearer's balance with backing **if** it is a redemption
            if (is_redemption) {
               d.adjust_balance(op.bearer, _redemption_amount);

               // Virtual operation for account history
               db().push_applied_operation(nft_redeemed_operation(op.bearer, op.amount, _redemption_amount));
            }

            return void_result();
         } FC_CAPTURE_AND_RETHROW((op))
      }

      void_result nft_burn_evaluator::do_evaluate(const nft_burn_operation &op) {
         try {
            const graphene::chain::database& d = db();

            // Reject any activity before the hardfork time
            auto now = d.head_block_time();
            FC_ASSERT(HARDFORK_NFT_M2_PASSED(now), "NFT Burning is not yet enabled");

            // Verify the existence of the associated asset
            const asset_object &t = op.amount.asset_id(d);

            // Verify that the asset is a minted token object
            const auto& token_id_idx = d.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
            auto token_itr = token_id_idx.find(op.amount.asset_id);
            const bool& is_nft = (token_itr != token_id_idx.end());
            FC_ASSERT(is_nft, "Burns may only be performed for NFT tokens");
            const nft_token_object &nft_obj = *token_itr;

            const auto &series_idx = d.get_index_type<nft_series_index>().indices().get<by_nft_series_asset_id>();
            auto series_itr = series_idx.find(nft_obj.series_id);
            FC_ASSERT(series_itr != series_idx.end());
            const nft_series_object &series_obj = *series_itr;

            // Verify that the asserted issuer is the Series Issuer
            const asset_object& series_asset_obj = series_obj.asset_id(d);
            const account_object& series_issuer = series_asset_obj.issuer(d);
            FC_ASSERT(op.issuer == series_issuer.id,
                      "Burns may only be initiated by the Series Issuer. (Series Issuer is ${series_issuer}.  Alleged issuer is ${op_issuer}.)",
                      ("series_issuer", series_issuer.id)
                      ("op_issuer", op.issuer)
            );

            // Verify presumptions
            const asset_dynamic_data_object &t_addo = t.dynamic_asset_data_id(d);
            _token_dyn_data = &t_addo;
            FC_ASSERT(nft_obj.amount_in_inventory > 0, "A burn is impossible because the Inventory is empty");
            FC_ASSERT(nft_obj.amount_in_inventory <= t_addo.current_supply.value); // Any difference should be present in other balances or previously burnt

            // Verify that the Inventory has sufficient assets to satisfy the burn
            FC_ASSERT(op.amount.asset_id == nft_obj.token_id); // Redundant check from earlier in the function
            FC_ASSERT(op.amount.amount > 0); // Redundant check from the operation's validate()
            FC_ASSERT(op.amount.amount <= nft_obj.amount_in_inventory,
                      "The amount of the burn (${requested} subdivisions) exceeds the available balance in the Inventory (${available} subdivisions)",
                      ("requested", op.amount.amount)
                         ("available", nft_obj.amount_in_inventory)
            );
            // Verify consistency with token supply
            FC_ASSERT(op.amount.amount <= t_addo.current_supply.value,
                      "The amount of the burn (${requested} subdivisions) exceeds the current supply of the token (${available} subdivisions)",
                      ("requested", op.amount.amount)
                         ("available", t_addo.current_supply.value)
            );

            // Verify the proposed new quantity in Inventory
            const fc::uint128_t &proposed_new_supply = fc::uint128_t(nft_obj.amount_in_inventory.value)
                                                       - op.amount.amount.value;
            FC_ASSERT(proposed_new_supply >= 0,
                      "Burning should NOT result in a negative supply of the token");

            // The following checks should pass easily but check in case
            // the other checks have overlooked an issue such as numerical overflow or underflow
            const share_type& t_whole_token = asset::scaled_precision(t.precision).value;
            const share_type& t_max_supply = t_whole_token;
            FC_ASSERT(proposed_new_supply <= t_max_supply,
                      "Burning would result in more subdivisions than are possible in a single whole token");

            return void_result();
         } FC_CAPTURE_AND_RETHROW((op))
      }

      object_id_type nft_burn_evaluator::do_apply(const nft_burn_operation &op) {
         try {
            graphene::chain::database &d = db();

            const share_type &amount_to_burn = op.amount.amount;

            const auto& token_id_idx = d.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
            auto token_itr = token_id_idx.find(op.amount.asset_id);

            // Modify the existing internal object
            const nft_token_object &obj = *token_itr;
            db().modify(obj, [&amount_to_burn](nft_token_object &data) {
               data.amount_burned += amount_to_burn;
               data.amount_in_inventory -= amount_to_burn;
            });

            // Modify the current supply of the token
            db().modify(*_token_dyn_data, [&amount_to_burn](asset_dynamic_data_object &data) {
               data.current_supply -= amount_to_burn;
            });

            return obj.id;
         } FC_CAPTURE_AND_RETHROW((op))
      }

      bool is_nft_royalty_claim(const database &db, const asset_id_type& asset_id) {
         const auto& series_royalty_idx = db.get_index_type<nft_series_index>().indices().get<by_nft_series_asset_id>();
         auto series_itr = series_royalty_idx.find(asset_id);
         const bool& is_nft_royalty_claim = (series_itr != series_royalty_idx.end());

         return is_nft_royalty_claim;
      }

      const nft_pending_royalty_claim_transfer evaluate_royalty_claim_transfer(const database &db,
                                                                               const account_id_type &from,
                                                                               const asset &amount,
                                                                               const account_id_type &to) {
         // Validate the input
         FC_ASSERT(0 < amount.amount);
         FC_ASSERT(amount.amount <= NFT_ROYALTY_CLAIMS_COUNT);

         // Find the NFT Series
         const auto& series_royalty_idx = db.get_index_type<nft_series_index>().indices().get<by_nft_series_asset_id>();
         auto series_itr = series_royalty_idx.find(amount.asset_id);
         const bool& is_nft_royalty_claim = (series_itr != series_royalty_idx.end());
         FC_ASSERT(is_nft_royalty_claim);

         // From
         const nft_series_object &series_obj = *series_itr;
         auto itr = series_obj.royalty_claims.find(from);
         FC_ASSERT(itr != series_obj.royalty_claims.end());
         const share_type &from_old_balance = itr->second;
         const share_type &from_new_balance = from_old_balance - amount.amount;
         FC_ASSERT(0 <= from_new_balance);
         FC_ASSERT(from_new_balance <= NFT_ROYALTY_CLAIMS_COUNT);

         // To
         share_type to_old_balance = 0;
         itr = series_obj.royalty_claims.find(to);
         if (itr != series_obj.royalty_claims.end()) {
            to_old_balance = itr->second;
         }
         const share_type &to_new_balance = to_old_balance + amount.amount;
         FC_ASSERT(0 <= to_new_balance);
         FC_ASSERT(to_new_balance <= NFT_ROYALTY_CLAIMS_COUNT);

         return nft_pending_royalty_claim_transfer(&series_obj, from, from_new_balance, to, to_new_balance);
      }

      void apply_royalty_claim_transfer(database &db,
                                        const nft_pending_royalty_claim_transfer &rtx) {
         const nft_series_object &series_obj = *rtx.ptr_series_obj;
         db.modify(series_obj, [&rtx](nft_series_object &obj) {
            // From
            if (rtx.from_new_balance > 0) {
               obj.royalty_claims[rtx.from] = rtx.from_new_balance;

            } else {
               // Equal to 0. Erase the entry.
               auto itr = obj.royalty_claims.find(rtx.from);
               if (itr != obj.royalty_claims.end()) {
                  obj.royalty_claims.erase(itr);
               } else {
                  // Should not ever be the case because of the prior check in evaluate_royalty_claim_transfer()
               }
            }

            // To
            obj.royalty_claims[rtx.to] = rtx.to_new_balance;
         });
      }

   } // namespace chain
} // namespace graphene
