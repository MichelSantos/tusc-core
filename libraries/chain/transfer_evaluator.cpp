/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/transfer_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

namespace graphene { namespace chain {
      // Round up the percentage calculation to the next higher integer
      //
      // Following the approach described at
      // https://blog.demofox.org/2022/07/21/rounding-modes-for-integer-division
      //   Floor:    A / B
      //   Round:   (A+B/2) / B
      //   Ceiling: (A+B-1) / B or alternately: ((A-1) / B) + 1
      //
      // Contrast with calculate_percent() of db_market.cpp
      share_type calculate_percent_and_round_up(const share_type &value, uint16_t percent) {
         if (value == 0 || percent == 0) {
            return share_type(0);
         }
         FC_ASSERT(0 <= value);
         FC_ASSERT(percent <= GRAPHENE_100_PERCENT);

         const fc::uint128_t A = fc::uint128_t(value.value) * percent;
         const uint32_t B(GRAPHENE_100_PERCENT);
         const fc::uint128_t C = ((A - 1) / B) + 1;

         // TODO: Possibly check the current (non-initial) supply or maximum supply of the relevant asset
         FC_ASSERT(C <= GRAPHENE_INITIAL_MAX_SHARE_SUPPLY, "Overflow when calculating percent");

         return static_cast<int64_t>(C);
      }

void_result transfer_evaluator::do_evaluate( const transfer_operation& op )
{ try {
   
   const database& d = db();

   const account_object& from_account    = op.from(d);
   const account_object& to_account      = op.to(d);
   const asset_object&   asset_type      = op.amount.asset_id(d);

   try {

      GRAPHENE_ASSERT(
         is_authorized_asset( d, from_account, asset_type ),
         transfer_from_account_not_whitelisted,
         "'from' account ${from} is not whitelisted for asset ${asset}",
         ("from",op.from)
         ("asset",op.amount.asset_id)
         );
      GRAPHENE_ASSERT(
         is_authorized_asset( d, to_account, asset_type ),
         transfer_to_account_not_whitelisted,
         "'to' account ${to} is not whitelisted for asset ${asset}",
         ("to",op.to)
         ("asset",op.amount.asset_id)
         );

      if( asset_type.is_transfer_restricted() )
      {
         GRAPHENE_ASSERT(
            from_account.id == asset_type.issuer || to_account.id == asset_type.issuer,
            transfer_restricted_transfer_asset,
            "Asset ${asset} has transfer_restricted flag enabled",
            ("asset", op.amount.asset_id)
          );
      }

      bool insufficient_balance = d.get_balance( from_account, asset_type ).amount >= op.amount.amount;
      FC_ASSERT( insufficient_balance,
                 "Insufficient Balance: ${balance}, unable to transfer '${total_transfer}' from account '${a}' to '${t}'", 
                 ("a",from_account.name)("t",to_account.name)("total_transfer",d.to_pretty_string(op.amount))("balance",d.to_pretty_string(d.get_balance(from_account, asset_type))) );

      // NFT Royalty payments are required for the transfer (Secondary Transfer)
      // when the asset being transferred
      // (a) is an NFT,
      // (b) having a positive minimum price per subdivision, and
      // (c) belongs to an NFT Series with a positive royalty fee percentage
      const auto& token_id_idx = d.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
      auto token_itr = token_id_idx.find(op.amount.asset_id);
      const bool& is_nft = (token_itr != token_id_idx.end());
      if (is_nft) {
         // Reject any activity before the hardfork time
         auto now = d.head_block_time();
         FC_ASSERT(HARDFORK_NFT_M3_PASSED(now), "NFT Secondary Transfer is not yet enabled");

         // Determine whether a royalty payment is required for this NFT
         const nft_token_object &nft_obj = *token_itr;
         const bool positive_min_price = nft_obj.min_price_per_subdivision.amount > 0;

         const auto &series_idx = d.get_index_type<nft_series_index>().indices().get<by_nft_series_asset_id>();
         auto series_itr = series_idx.find(nft_obj.series_id);
         FC_ASSERT(series_itr != series_idx.end());
         const nft_series_object &series_obj = *series_itr;
         const bool positive_series_rfp = series_obj.royalty_fee_centipercent > 0;

         const bool is_nft_royalty_required = positive_series_rfp && positive_min_price;

         if (is_nft_royalty_required) {
            _ptr_nft_obj = &nft_obj; // Preserve the pointer for later use by do_apply()

            const asset_object& royalty_asset_obj = nft_obj.min_price_per_subdivision.asset_id(d);
            const asset_dynamic_data_object &royalty_asset_addo = royalty_asset_obj.dynamic_asset_data_id(d);
            const share_type royalty_asset_supply = royalty_asset_addo.current_supply;

            // Calculate the royalty required
            const share_type& transfer_amount = op.amount.amount;
            const fc::uint128_t &min_sale_amount_uint128 = fc::uint128_t(nft_obj.min_price_per_subdivision.amount.value) * transfer_amount.value;
            FC_ASSERT(min_sale_amount_uint128 <= royalty_asset_supply,
                      "Overflow when calculating the required royalty fee for a secondary transfer");
            share_type min_sale_amount = static_cast<int64_t>(min_sale_amount_uint128);

            share_type royalty_amount = calculate_percent_and_round_up(min_sale_amount,
                                                                       series_obj.royalty_fee_centipercent);
            FC_ASSERT(royalty_amount >= 0, "Royalty payments must be non-negative");
            _nft_royalty = royalty_asset_obj.amount(royalty_amount);

            // Check whether the sender has sufficient balance to pay the royalty payment
            insufficient_balance = d.get_balance(from_account, royalty_asset_obj).amount >= _nft_royalty.amount.value;
            FC_ASSERT(insufficient_balance,
                      "Insufficient Balance to pay NFT royalty: A transfer of '${total_transfer}' from account '${a}' to '${t}' requires payment of '${r}' but sender's balance is only `${balance}`",
                      ("a",from_account.name)("t",to_account.name)("total_transfer",d.to_pretty_string(op.amount))
                      ("balance",d.to_pretty_string(d.get_balance(from_account, royalty_asset_obj)))
                      ("r",d.to_pretty_string(_nft_royalty))
            );
         }
      }

      return void_result();
   } FC_RETHROW_EXCEPTIONS( error, "Unable to transfer ${a} from ${f} to ${t}", ("a",d.to_pretty_string(op.amount))("f",op.from(d).name)("t",op.to(d).name) );

}  FC_CAPTURE_AND_RETHROW( (op) ) }

void_result transfer_evaluator::do_apply( const transfer_operation& o )
{ try {
   db().adjust_balance( o.from, -o.amount );
   db().adjust_balance( o.to, o.amount );

   if (_ptr_nft_obj != nullptr) {
      // Deduct from sender
      db().adjust_balance(o.from, -_nft_royalty);

      // Allocate the royalty to the NFT's royalty reservoir
      const nft_token_object &obj = *_ptr_nft_obj;
      const share_type &royalty_amount = _nft_royalty.amount;
      db().modify(obj, [&royalty_amount](nft_token_object &data) {
         data.royalty_reservoir.amount += royalty_amount;
      });

      // Virtual operation for account history
      const nft_royalty_paid_operation &vop = nft_royalty_paid_operation(o.amount, _nft_royalty, o.from);
      db().push_applied_operation(vop);
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }



void_result override_transfer_evaluator::do_evaluate( const override_transfer_operation& op )
{ try {
   const database& d = db();

   const asset_object&   asset_type      = op.amount.asset_id(d);
   GRAPHENE_ASSERT(
      asset_type.can_override(),
      override_transfer_not_permitted,
      "override_transfer not permitted for asset ${asset}",
      ("asset", op.amount.asset_id)
      );
   FC_ASSERT( asset_type.issuer == op.issuer );

   const account_object& from_account    = op.from(d);
   const account_object& to_account      = op.to(d);

   FC_ASSERT( is_authorized_asset( d, to_account, asset_type ) );
   FC_ASSERT( is_authorized_asset( d, from_account, asset_type ) );

   FC_ASSERT( d.get_balance( from_account, asset_type ).amount >= op.amount.amount,
              "", ("total_transfer",op.amount)("balance",d.get_balance(from_account, asset_type).amount) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result override_transfer_evaluator::do_apply( const override_transfer_operation& o )
{ try {
   db().adjust_balance( o.from, -o.amount );
   db().adjust_balance( o.to, o.amount );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

} } // graphene::chain
