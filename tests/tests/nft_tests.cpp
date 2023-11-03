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

#include <boost/test/unit_test.hpp>

#include <fc/optional.hpp>

#include <graphene/protocol/nft.hpp> // For NFT protocol operation
#include <graphene/chain/nft_object.hpp> // For NFT implementaion object
#include <graphene/chain/asset_object.hpp> // For asset implementaion object
#include <graphene/chain/proposal_object.hpp> // For proposal implementation object

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/app/database_api.hpp> // For testing current state
#include <graphene/app/api.hpp> // For testing account history
#include <graphene/nft_history/nft_history.hpp> // For testing NFT history

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;

struct nft_database_fixture : database_fixture {
   nft_database_fixture()
      : database_fixture() {
   }

   void advance_past_m1_hardfork() {
      generate_blocks(HARDFORK_NFT_M1_TIME);
      set_expiration(db, trx);
   }

   void advance_past_m2_hardfork() {
      generate_blocks(HARDFORK_NFT_M2_TIME);
      set_expiration(db, trx);
   }

   void advance_past_m3_hardfork() {
      generate_blocks(HARDFORK_NFT_M3_TIME);
      set_expiration(db, trx);
   }

   void advance_past_m4_hardfork() {
      generate_blocks(HARDFORK_NFT_M4_TIME);
      set_expiration(db, trx);
   }

   // Assets that may be associated with an NFT Series should not retain the
   // charge_market_fee permission at creation
   // Use a bitmask to include any permission except charge_market_fee
   const uint16_t UIA_EXCEPT_CHARGE_MARKET_FEE = DEFAULT_UIA_ASSET_ISSUER_PERMISSION & ~charge_market_fee;
   // Prepare the asset for use as an NFT Series
   void prepare_uia_for_nft_series(const asset_id_type uia_id,
                                   const account_id_type issuer_id, const private_key_type issuer_priv_key) {
      asset_update_operation uop;
      uop.asset_to_update = uia_id;
      uop.issuer = issuer_id;
      uop.new_options = uia_id(db).options;
      // Merge the existing permissions and flags with:
      //   Disable new supply
      //   Lock the maximum supply
      uop.new_options.issuer_permissions = uop.new_options.issuer_permissions | disable_new_supply | lock_max_supply;
      uop.new_options.flags = uop.new_options.flags | disable_new_supply | lock_max_supply;
      trx.clear();
      trx.operations.push_back(uop);
      sign(trx, issuer_priv_key);
      PUSH_TX(db, trx);
   }

   // Create and asset and series from a name
   const asset_id_type create_asset_and_series(string series_name,
                                               account_id_type issuer_id, private_key_type issuer_priv_key,
                                               account_id_type beneficiary_id,
                                               account_id_type manager_id,
                                               uint16_t royalty_fee_centipercent) {
      // Creates an asset
      const asset_object& series_asset = create_user_issued_asset(series_name, issuer_id(db), 0);
      const asset_id_type series_asset_id = series_asset.id;

      // Create the series from the asset
      nft_series_create_operation create_op;
      create_op.issuer = issuer_id;
      create_op.asset_id = series_asset_id;
      create_op.beneficiary = beneficiary_id;
      create_op.manager = manager_id;
      create_op.royalty_fee_centipercent = royalty_fee_centipercent;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, issuer_priv_key);
      PUSH_TX(db, trx);

      trx.clear();
      return series_asset_id;
   }

   // Create and sub-asset and mint it into an existing series
   const asset_id_type create_sub_asset_and_mint(const string sub_asset_name, const uint8_t sub_asset_precision,
                                                 account_id_type issuer_id, private_key_type issuer_priv_key,
                                                 asset req_backing_per_subdivision, asset min_price_per_subdivision,
                                                 uint16_t flags = 0) {
      // Create the sub-asset
      const uint64_t& whole_token_subdivisions = asset::scaled_precision(sub_asset_precision).value;
      const uint64_t& max_supply = whole_token_subdivisions;
      const asset_object &sub_asset_obj = create_user_issued_asset(sub_asset_name, issuer_id(db), flags,
                                                                   max_supply, sub_asset_precision);
      const asset_id_type sub_asset_id = sub_asset_obj.id;

      // Mint the sub-asset into a series
      graphene::chain::nft_mint_operation mint_op;
      mint_op.issuer = issuer_id;
      mint_op.asset_id = sub_asset_id;
      mint_op.subdivisions = whole_token_subdivisions;
      mint_op.min_price_per_subdivision = min_price_per_subdivision;
      mint_op.req_backing_per_subdivision = req_backing_per_subdivision;
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, issuer_priv_key);
      PUSH_TX(db, trx);

      trx.clear();
      return sub_asset_id;
   }

   const limit_order_create_operation create_sell_operation(const account_id_type &seller,
                                                            const asset &amount,
                                                            const asset &recv,
                                                            const time_point_sec order_expiration) {
      limit_order_create_operation sell_order;
      sell_order.seller = seller;
      sell_order.amount_to_sell = amount;
      sell_order.min_to_receive = recv;
      sell_order.expiration = order_expiration;

      return sell_order;
   }

   // Create a UIA with a specified precision and market fee percentage
   const asset_object& create_uia(const string &name, const account_object &issuer,
                                  uint16_t flags, const uint64_t max_supply,
                                  const uint8_t precision, uint16_t market_fee_percent) {
      asset_create_operation creator;
      creator.issuer = issuer.id;
      creator.fee = asset();
      creator.symbol = name;
      creator.precision = precision;
      creator.common_options.core_exchange_rate = price(asset(1, asset_id_type(1)), asset(1));
      creator.common_options.initial_max_supply = max_supply;
      creator.common_options.flags = flags;
      creator.common_options.issuer_permissions = flags;
      creator.common_options.market_fee_percent = market_fee_percent;
      trx.operations.clear();
      trx.operations.push_back(std::move(creator));
      trx.validate();
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      trx.operations.clear();
      return db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
   }

   // Test secondary sales of single-subdivision assets
   // subject to a particular market fee percentage
   void test_single_division_secondary_sales(uint16_t market_fee_percent);
};


BOOST_FIXTURE_TEST_SUITE( nft_tests, nft_database_fixture
)

/**
 * Test a simple and valid creation of an NFT Series
 */
BOOST_AUTO_TEST_CASE( nft_series_creation_a ) {
   try {
      // Initialize
      ACTORS((alice)(bob));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, alice_id, graphene::chain::asset(init_balance));
      transfer(committee_account, bob_id, graphene::chain::asset(init_balance));
      advance_past_m1_hardfork();
      const account_id_type invalid_account_id(9999999); // Non-existent account

      // Alice creates an asset
      const string series_name = "SERIESA";
      const asset_object &pre_existing_asset = create_user_issued_asset(series_name, alice_id(db), 0);
      const asset_id_type pre_existing_asset_id = pre_existing_asset.id;

      // Reject series creation by an account that does not control the associated asset
      // Bob attempts an invalid series creation with Alice's asset
      BOOST_TEST_MESSAGE("Bob is attempting to create an NFT Series with Alice's asset");
      graphene::chain::nft_series_create_operation create_op;
      create_op.issuer = bob_id;
      create_op.asset_id = pre_existing_asset_id;
      create_op.beneficiary = bob_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, bob_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Incorrect issuer");

      // Reject series creation due to invalid royalty percentage
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = pre_existing_asset_id;
      create_op.beneficiary = alice_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = GRAPHENE_100_PERCENT + 1;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Reject series creation due to invalid beneficiary
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = pre_existing_asset_id;
      create_op.beneficiary = invalid_account_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Reject series creation due to invalid manager
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = pre_existing_asset_id;
      create_op.manager = invalid_account_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Alice attempts to create the series from her asset
      BOOST_TEST_MESSAGE("Alice is creating an NFT Series");
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = pre_existing_asset_id;
      create_op.beneficiary = alice_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Verify whether the object is accessible before the next block has been generated
      BOOST_TEST_MESSAGE("Checking the series implementation object");
      const auto &series_name_idx = db.get_index_type<nft_series_index>().indices().get<by_nft_series_name>();
      auto series_itr = series_name_idx.find(series_name);
      BOOST_REQUIRE(series_itr != series_name_idx.end());

      // Verify whether the associated asset is accessible
      BOOST_TEST_MESSAGE("Checking the asset associated with the series");
      const auto &asset_idx = db.get_index_type<asset_index>().indices().get<by_symbol>();
      auto asset_itr = asset_idx.find(series_name);
      BOOST_REQUIRE(asset_itr != asset_idx.end());

      // Verify that the IDs match
      const nft_series_object &series_obj = *series_itr;
      const asset_object &asset_obj = *asset_itr;
      BOOST_REQUIRE(series_obj.asset_id == asset_obj.id);

      BOOST_TEST_MESSAGE("Verifying NFT Series properties");
      BOOST_REQUIRE(asset_obj.issuer == alice.id);
      BOOST_REQUIRE(series_obj.beneficiary == alice.id);
      BOOST_REQUIRE(series_obj.manager == alice.id);
      BOOST_CHECK_EQUAL(series_obj.royalty_fee_centipercent, 0);

      BOOST_TEST_MESSAGE("Verifying NFT Series did not add any issued asset to the creator's balance");
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, series_obj.asset_id), 0);

      // Advance to the next block
      generate_block();
      trx.clear();
      set_expiration(db, trx);

      // Check that a duplicate attempt fails
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "already a series");

      // Check that a similar attempt fails
      trx.clear();
      create_op.beneficiary = bob_id;
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "already a series");

   } FC_LOG_AND_RETHROW()
}

/**
 * Test the rejection of a series creation due to prohibitions against sub-assets
 */
BOOST_AUTO_TEST_CASE( nft_series_creation_invalid_a ) {
   try {
      // Initialize
      ACTORS((alice)(bob));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, alice_id, graphene::chain::asset(init_balance));
      transfer(committee_account, bob_id, graphene::chain::asset(init_balance));
      advance_past_m1_hardfork();
      graphene::chain::nft_series_create_operation create_op;

      // Alice creates an asset
      const string series_name = "SERIESA";
      const asset_object &pre_existing_asset = create_user_issued_asset(series_name, alice_id(db), 0);
      const asset_id_type pre_existing_asset_id = pre_existing_asset.id;

      // Alice creates a sub-asset
      const string sub_asset_name = "SERIESA.SUB1";
      const asset_object &pre_existing_sub_asset = create_user_issued_asset(sub_asset_name, alice_id(db), 0);
      const asset_id_type pre_existing_sub_asset_id = pre_existing_sub_asset.id;

      // Alice creates an asset
      const string series_b_name = "SERIESB";
      const asset_object &asset_b = create_user_issued_asset(series_b_name, alice_id(db), 0);
      const asset_id_type asset_b_id = asset_b.id;

      // Alice creates an asset with name so long that it will prevent sub-assets
      // GRAPHENE_MAX_ASSET_SYMBOL_LENGTH is set to 16.
      // Therefore, the maximum name length for an NFT SERIES should be 14.
      const string series_name_too_long = "SERIES789012345";
      const asset_object &asset_too_long = create_user_issued_asset(series_name_too_long, alice_id(db), 0);
      const asset_id_type asset_too_long_id = asset_too_long.id;

      // Alice creates an asset with name that is barely short enough that it will permit sub-assets
      // GRAPHENE_MAX_ASSET_SYMBOL_LENGTH is set to 16.
      // Therefore, the maximum name length for an NFT SERIES should be 14.
      const string series_name_short_enough = "SERIES78901234";
      const asset_object &asset_short_enough = create_user_issued_asset(series_name_short_enough, alice_id(db), 0);
      const asset_id_type asset_short_enough_id = asset_short_enough.id;

      // Reject series creation for a sub-asset
      // Alice attempts to create the series from her sub-asset
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = pre_existing_sub_asset_id;
      create_op.beneficiary = alice_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Reject series creation for an asset with sub-assets
      // Alice attempts to create the series for a parent asset
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = pre_existing_asset_id;
      create_op.beneficiary = alice_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Confirm valid series creation for an asset without sub-assets
      // Alice attempts to create the series for a solo asset
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = asset_b_id;
      create_op.beneficiary = alice_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Reject series creation with a name that is so long that it will
      // prevent the creation of sub-assets
      // because the Graphene-conventional sub-asset name will be too long
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = asset_too_long_id;
      create_op.beneficiary = alice_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "name is too long");

      // Confirm valid series creation with a name that is barely short enough to
      // permit the creation of sub-assets
      // by Graphene-conventional sub-asset names
      create_op = graphene::chain::nft_series_create_operation();
      create_op.issuer = alice_id;
      create_op.asset_id = asset_short_enough_id;
      create_op.beneficiary = alice_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

   } FC_LOG_AND_RETHROW()
}

/**
 * Test the rejection of a series creation prior to the hardfork
 * Test the rejection of a series creation in a proposal prior to the hardfork
 */
BOOST_AUTO_TEST_CASE( nft_series_creation_before_hardfork ) {
   try {
      // Initialize
      ACTORS((alice)(bob));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, alice_id, graphene::chain::asset(init_balance));
      transfer(committee_account, bob_id, graphene::chain::asset(init_balance));

      // Advance to before the hardfork time
      generate_blocks(HARDFORK_NFT_M1_TIME - 100);

      // Alice creates an asset
      const string series_name = "SERIESA";
      const asset_object &pre_existing_asset = create_user_issued_asset(series_name, alice_id(db), 0);
      const asset_id_type pre_existing_asset_id = pre_existing_asset.id;

      // Reject series creation before the hardfork
      // Alice attempts an valid series creation creation with Alice's asset
      BOOST_TEST_MESSAGE("Alice is attempting to create an NFT Series with Alice's asset");
      graphene::chain::nft_series_create_operation create_op;
      create_op.issuer = alice_id;
      create_op.asset_id = pre_existing_asset_id;
      create_op.beneficiary = alice_id;
      create_op.manager = alice_id;
      create_op.royalty_fee_centipercent = 0;
      trx.clear();
      trx.operations.push_back(create_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "not yet enabled");

      // Reject series creation in a proposal before the hardfork
      {
         proposal_create_operation pop;
         pop.review_period_seconds = 86400;
         uint32_t buffer_seconds = 60 * 60;
         pop.expiration_time = db.head_block_time() + *pop.review_period_seconds + buffer_seconds;
         pop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         pop.proposed_ops.emplace_back(create_op);

         trx.clear();
         trx.operations.push_back(pop);
         // sign(trx, alice_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Not allowed until");
      }

      // Advance to after the hardfork time
      generate_blocks(HARDFORK_NFT_M1_TIME);
      trx.clear();
      set_expiration(db, trx);

      // Attempt a proposal with an embedded series creation
      {
         proposal_create_operation pop;
         pop.review_period_seconds = 86400;
         uint32_t buffer_seconds = 60 * 60;
         pop.expiration_time = db.head_block_time() + *pop.review_period_seconds + buffer_seconds;
         pop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         pop.proposed_ops.emplace_back(create_op);

         trx.clear();
         trx.operations.push_back(pop);
         // sign(trx, alice_private_key);
         PUSH_TX(db, trx);
      }

   } FC_LOG_AND_RETHROW()
}

/**
 * Test a simple and valid minting of an NFT token
 */
BOOST_AUTO_TEST_CASE(nft_mint_a) {
   try {
      INVOKE(nft_series_creation_a);

      // Initialize
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      const string series_name = "SERIESA";
      const asset_id_type core_id = asset_id_type();
      graphene::chain::nft_mint_operation mint_op;

      ///
      /// Mint token #1
      ///
      // Create the sub-asset
      const string sub_asset_1_name = series_name + ".SUB1";
      const asset_object &sub_asset_1_obj = create_user_issued_asset(sub_asset_1_name, alice_id(db), 0, 1000000, 2);
      const asset_id_type sub_asset_1_id = sub_asset_1_obj.id;
      const asset_dynamic_data_object sub_asset_dd = sub_asset_1_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(sub_asset_1_obj.precision, 2);
      BOOST_CHECK_EQUAL(sub_asset_dd.current_max_supply.value, 1000000);
      BOOST_CHECK_EQUAL(sub_asset_1_obj.options.initial_max_supply.value, 1000000);

      // Confirm valid minting of a sub-asset into a series
      // Alice attempts to create the series for a solo asset
      BOOST_TEST_MESSAGE("Minting Token #1 into series");
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_1_id;
      mint_op.subdivisions = 100; // An entire single token (10^precision = 10^2 = 100)
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Verify the implementation object
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_obj = *token_itr;
      BOOST_REQUIRE(token_obj.min_price_per_subdivision == asset(0, core_id));
      BOOST_REQUIRE(token_obj.req_backing_per_subdivision == asset(0, core_id));
      BOOST_REQUIRE(token_obj.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_TEST_MESSAGE("Verifying the presence of tokens in the Series Inventory");
      BOOST_REQUIRE_EQUAL(token_obj.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(token_obj.amount_in_inventory.value, 100);
      BOOST_REQUIRE_EQUAL(token_obj.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(token_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_obj.current_backing.asset_id == core_id);

      ///
      /// Mint token #2
      ///
      // Create the sub-asset
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_object &sub_asset_2_obj = create_user_issued_asset(sub_asset_2_name, alice_id(db), 0, 1000000, 3);
      const asset_id_type sub_asset_2_id = sub_asset_2_obj.id;

      // Confirm valid minting of a sub-asset into a series
      // Alice attempts to create the series for a solo asset
      BOOST_TEST_MESSAGE("Minting Token #2 into series");
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_2_id;
      mint_op.subdivisions = 1000; // An entire single token (10^precision = 10^3 = 1000)
      mint_op.min_price_per_subdivision = asset(750, core_id);
      mint_op.req_backing_per_subdivision = asset(500, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Verify the implementation object
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_2_obj = *token_itr;
      BOOST_REQUIRE(token_2_obj.min_price_per_subdivision == asset(750, core_id));
      BOOST_REQUIRE(token_2_obj.req_backing_per_subdivision == asset(500, core_id));
      BOOST_REQUIRE(token_2_obj.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_TEST_MESSAGE("Verifying the presence of tokens in the Series Inventory");
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_in_inventory.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_2_obj.current_backing.asset_id == core_id);

   } FC_LOG_AND_RETHROW()
}

/**
 * Test minting and burning attempts after a change to an NFT's/sub-asset's issuer.
 * Note: The issuer of an NFT Series/parent asset is distinct from
 *       the issuer of of NFT Token/sub-asset.
 */
BOOST_AUTO_TEST_CASE(nft_mint_and_burn_with_different_b) {
   try {
      INVOKE(nft_series_creation_a);

      // Initialize
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      const string series_name = "SERIESA";
      const asset_id_type core_id = asset_id_type();

      // Create a random other account
      ACTOR(rando);
      const int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, rando_id, graphene::chain::asset(init_balance));
      graphene::chain::nft_mint_operation mint_op;

      ///
      /// Attempt to Mint token #1
      ///
      BOOST_TEST_MESSAGE("Testing Minting Sub-Assets with an Issuer Different than the Series");
      BOOST_TEST_MESSAGE("  Minting Token #2 into Series");
      // Create the sub-asset
      const string sub_asset_1_name = series_name + ".SUB1";
      // First attempt should fail because the Series Issuer is not designated as the sub-asset Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(create_user_issued_asset(sub_asset_1_name, rando_id(db), 0, 1000000, 2),
                                  "may only be created by issuer of SERIESA");
      const asset_object &sub_asset_1_obj = create_user_issued_asset(sub_asset_1_name, alice_id(db), 0, 1000000, 2);
      const asset_id_type sub_asset_1_id = sub_asset_1_obj.id;
      const asset_dynamic_data_object sub_asset_dd = sub_asset_1_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(sub_asset_1_obj.precision, 2);
      BOOST_CHECK_EQUAL(sub_asset_dd.current_max_supply.value, 1000000);
      BOOST_CHECK_EQUAL(sub_asset_1_obj.options.initial_max_supply.value, 1000000);

      // Before any minting of the sub-asset, change its issuer
      asset_update_issuer_operation update_op;
      update_op.asset_to_update = sub_asset_1_id;
      update_op.issuer = alice_id;
      update_op.new_issuer = rando_id;
      trx.clear();
      trx.operations.push_back(update_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Verify new issuer
      BOOST_REQUIRE(get_asset(sub_asset_1_name).issuer == rando_id);

      // Rando attempts to mint the sub-asset into the series
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = rando_id;
      mint_op.asset_id = sub_asset_1_id;
      mint_op.subdivisions = 35; // An entire single token consists of 10^precision = 10^2 = 100
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, rando_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Minting may only be initiated by the Series Issuer");

      // Alice attempts to mint the sub-asset into the series
      mint_op.issuer = alice_id;
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx); // Should succeed

      // Verify the implementation object
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_obj = *token_itr;
      BOOST_REQUIRE(token_obj.min_price_per_subdivision == asset(0, core_id));
      BOOST_REQUIRE(token_obj.req_backing_per_subdivision == asset(0, core_id));
      BOOST_REQUIRE(token_obj.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_TEST_MESSAGE("Verifying the presence of tokens in the Series Inventory");
      BOOST_REQUIRE_EQUAL(token_obj.amount_minted.value, 35);
      BOOST_REQUIRE_EQUAL(token_obj.amount_in_inventory.value, 35);
      BOOST_REQUIRE_EQUAL(token_obj.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(token_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_obj.current_backing.asset_id == core_id);

      ///
      /// Mint token #2
      ///
      BOOST_TEST_MESSAGE("  Minting Token #2 into Series");
      // Create the sub-asset
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_object &sub_asset_2_obj = create_user_issued_asset(sub_asset_2_name, alice_id(db), 0, 1000000, 3);
      const asset_id_type sub_asset_2_id = sub_asset_2_obj.id;

      // Before any minting of the sub-asset, change its issuer
      update_op = asset_update_issuer_operation();
      update_op.asset_to_update = sub_asset_2_id;
      update_op.issuer = alice_id;
      update_op.new_issuer = rando_id;
      trx.clear();
      trx.operations.push_back(update_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Verify new issuer
      BOOST_REQUIRE(get_asset(sub_asset_2_name).issuer == rando_id);

      // Rando attempts to create the series for the sub-asset
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = rando_id;
      mint_op.asset_id = sub_asset_2_id;
      mint_op.subdivisions = 350; // An entire single token = 10^precision = 10^3 = 1000 subdivisions
      mint_op.min_price_per_subdivision = asset(750, core_id);
      mint_op.req_backing_per_subdivision = asset(500, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, rando_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Minting may only be initiated by the Series Issuer");

      // Confirm valid burning of a Series NFT/sub-asset
      // Alice attempts to create the series for the sub-asset
      mint_op.issuer = alice_id;
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx); // Should succeed

      // Verify the implementation object
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_2_obj = *token_itr;
      BOOST_REQUIRE(token_2_obj.min_price_per_subdivision == asset(750, core_id));
      BOOST_REQUIRE(token_2_obj.req_backing_per_subdivision == asset(500, core_id));
      BOOST_REQUIRE(token_2_obj.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_TEST_MESSAGE("Verifying the presence of tokens in the Series Inventory");
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_minted.value, 350);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_in_inventory.value, 350);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_2_obj.current_backing.asset_id == core_id);

      ///
      /// Burn Token #2
      ///
      BOOST_TEST_MESSAGE("  Burning Token #2 from Series");
      advance_past_m2_hardfork();
      // Use an updated Token #2 reference after block production
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_obj_2 = *token_itr;

      // Rando attempts to Burn 10% of Token #1
      // which should fail because Alice is the Series issuer
      graphene::chain::nft_burn_operation burn_op;
      burn_op.issuer = rando_id;
      burn_op.amount = asset(100, sub_asset_2_id); // 100 of the 350 subdivisions present in the Inventory
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, rando_private_key); // Should be signed by the Series Issuer but is being signed by another account
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Burns may only be initiated by the Series Issuer");

      // Confirm valid burning of a Series NFT/sub-asset
      // Alice attempts to burn the sub-asset
      burn_op.issuer = alice_id;
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx); // Should succeed

      // Verify the implementation object
      BOOST_REQUIRE(t2_obj_2.min_price_per_subdivision == asset(750, core_id));
      BOOST_REQUIRE(t2_obj_2.req_backing_per_subdivision == asset(500, core_id));
      BOOST_REQUIRE(t2_obj_2.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_TEST_MESSAGE("Verifying the presence of tokens in the Series Inventory");
      BOOST_REQUIRE_EQUAL(t2_obj_2.amount_minted.value, 350);
      BOOST_REQUIRE_EQUAL(t2_obj_2.amount_in_inventory.value, 250);
      BOOST_REQUIRE_EQUAL(t2_obj_2.amount_burned.value, 100);
      BOOST_REQUIRE_EQUAL(t2_obj_2.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(t2_obj_2.current_backing.amount.value, 0);
      BOOST_REQUIRE(t2_obj_2.current_backing.asset_id == core_id);


      ///
      /// Transfer Series ownership and attempt burning again
      /// Alice transfers the Series to Bob
      ///
      BOOST_TEST_MESSAGE("  Transferring Series");
      const asset_id_type series_asset_id = get_asset(series_name).id;

      update_op = asset_update_issuer_operation();
      update_op.asset_to_update = series_asset_id;
      update_op.issuer = alice_id;
      update_op.new_issuer = bob_id;
      trx.clear();
      trx.operations.push_back(update_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Verify new issuer
      BOOST_REQUIRE(get_asset(series_name).issuer == bob_id);

      // Alice attempts to Burn 5% of Token #1
      // which should fail because Bob is the Series issuer
      BOOST_TEST_MESSAGE("  Burning Token #2 from Series");
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(50, sub_asset_2_id); // 50 of the 250 subdivisions present in the Inventory
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer but is being signed by another account
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Burns may only be initiated by the Series Issuer");

      // Confirm valid burning of a Series NFT/sub-asset
      // Bob attempts to burn the sub-asset
      burn_op.issuer = bob_id;
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, bob_private_key);
      PUSH_TX(db, trx); // Should succeed

      // Verify the implementation object
      BOOST_REQUIRE(t2_obj_2.min_price_per_subdivision == asset(750, core_id));
      BOOST_REQUIRE(t2_obj_2.req_backing_per_subdivision == asset(500, core_id));
      BOOST_REQUIRE(t2_obj_2.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_REQUIRE_EQUAL(t2_obj_2.amount_minted.value, 350);
      BOOST_REQUIRE_EQUAL(t2_obj_2.amount_in_inventory.value, 200);
      BOOST_REQUIRE_EQUAL(t2_obj_2.amount_burned.value, 150);
      BOOST_REQUIRE_EQUAL(t2_obj_2.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(t2_obj_2.current_backing.amount.value, 0);
      BOOST_REQUIRE(t2_obj_2.current_backing.asset_id == core_id);

   } FC_LOG_AND_RETHROW()
}

/**
 * Test rejection of minting of an NFT token with invalid properties
 */
BOOST_AUTO_TEST_CASE( nft_minting_invalid_a ) {
   try {
      INVOKE(nft_series_creation_a);

      // Initialize
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      const string series_name = "SERIESA";
      const asset_id_type core_id = asset_id_type();
      graphene::chain::nft_mint_operation mint_op;

      // Create the sub-asset
      const string sub_asset_1_name = series_name + ".SUB1";
      const asset_object &sub_asset_1_obj = create_user_issued_asset(sub_asset_1_name, alice_id(db), 0, 1000000, 2);
      const asset_id_type sub_asset_1_id = sub_asset_1_obj.id;
      const asset_dynamic_data_object sub_asset_1_dd = sub_asset_1_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(sub_asset_1_obj.precision, 2);
      BOOST_CHECK_EQUAL(sub_asset_1_dd.current_max_supply.value, 1000000);
      BOOST_CHECK_EQUAL(sub_asset_1_obj.options.initial_max_supply.value, 1000000);


      ///
      /// Test rejection of minting a token by someone other than the issuer
      ///
      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_1_id;
      mint_op.subdivisions = 100; // An entire single token (10^precision = 10^2 = 100)
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, bob_private_key); // <-- Bob is attempting to mint Alice's token
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);


      ///
      /// Test rejection of minting a token that is not a sub-asset
      ///
      const string other_asset_name = "OTHER";
      const asset_object& other_asset_obj = create_user_issued_asset(other_asset_name, alice_id(db), 0, 2000, 2);
      const asset_id_type other_asset_id = other_asset_obj.id;

      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = other_asset_id;
      mint_op.subdivisions = 100; // An entire single token (10^precision = 10^2 = 100)
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "should be a sub-asset");


      ///
      /// Test rejection of minting a token that could permit more subdivisions than are possible in a single "whole" token
      ///
      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_1_id;
      mint_op.subdivisions = 100 + 1; // An entire single token (10^precision = 10^2 = 100) + 1
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "whole token is prohibited");


      ///
      /// Test rejection of minting a token that could permit more subdivisions than the maximum supply
      ///
      // Create the sub-asset
      // Precision of 2 would imply that a whole token would consist of 100 subdivision
      // But the asset will be constructed to have a maximum supply of merely 95 subdivisions
      // Then attempt to mint the asset with 98 subdivisions
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_object &sub_asset_2_obj = create_user_issued_asset(sub_asset_2_name, alice_id(db), 0, 95, 2);
      const asset_id_type sub_asset_2_id = sub_asset_2_obj.id;
      const asset_dynamic_data_object sub_asset_2_dd = sub_asset_2_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(sub_asset_2_obj.precision, 2);
      BOOST_CHECK_EQUAL(sub_asset_2_dd.current_max_supply.value, 95);
      BOOST_CHECK_EQUAL(sub_asset_2_obj.options.initial_max_supply.value, 95);

      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_2_id;
      mint_op.subdivisions = 98; // Less than a whole token yet more than the maximum supply
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "maximum supply is prohibited");


      ///
      /// Test invalid minting: non-existent token
      ///
      const asset_id_type non_existent_asset_id(9999999); // Non-existent asset

      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = non_existent_asset_id;
      mint_op.subdivisions = 100;
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);


      ///
      /// Test invalid minting:
      /// - non-positive subdivisions
      /// - negative backing
      /// - negative price per subdivision
      /// - price < backing
      ///
      // Create the sub-asset with a maximum of 50 whole units
      const string sub_asset_3_name = series_name + ".SUB3";
      const asset_object &sub_asset_3_obj = create_user_issued_asset(sub_asset_3_name, alice_id(db), 0, 50000, 3);
      const asset_id_type sub_asset_3_id = sub_asset_3_obj.id;
      const asset_dynamic_data_object sub_asset_3_dd = sub_asset_3_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(sub_asset_3_obj.precision, 3);
      BOOST_CHECK_EQUAL(sub_asset_3_dd.current_max_supply.value, 50000);
      BOOST_CHECK_EQUAL(sub_asset_3_obj.options.initial_max_supply.value, 50000);

      // Attempt to mint the token into Inventory
      // with non-positive subdivisions
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_3_id;
      mint_op.subdivisions = -100;
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to mint the token into Inventory
      // with negative backing
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_3_id;
      mint_op.subdivisions = +100;
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(-5, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to mint the token into Inventory
      // with negative price per subdivision
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_3_id;
      mint_op.subdivisions = +100;
      mint_op.min_price_per_subdivision = asset(-5, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to mint the token into Inventory
      // price per subdivision < backing per subdivision
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_3_id;
      mint_op.subdivisions = +100;
      mint_op.min_price_per_subdivision = asset(5, core_id);
      mint_op.req_backing_per_subdivision = asset(6, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to mint the token into Inventory
      // with valid settings
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_3_id;
      mint_op.subdivisions = +100;
      mint_op.min_price_per_subdivision = asset(6, core_id);
      mint_op.req_backing_per_subdivision = asset(6, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);


      ///
      /// Test rejection of repeated mintings of a token THAT MIGHT EXCEED A WHOLE UNIT.
      /// Test case: An asset with a precision of 2 may only have a maximum of 100 subdivisions.
      ///
      // Create the sub-asset
      const string sub_asset_4_name = series_name + ".SUB4";
      const asset_object &sub_asset_4_obj = create_user_issued_asset(sub_asset_4_name, alice_id(db), 0, 2500, 2);
      const asset_id_type sub_asset_4_id = sub_asset_4_obj.id;
      const asset_dynamic_data_object& sub_asset_4_dd = sub_asset_4_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(sub_asset_4_obj.precision, 2);
      BOOST_CHECK_EQUAL(sub_asset_4_dd.current_supply.value, 0);
      BOOST_CHECK_EQUAL(sub_asset_4_dd.current_max_supply.value, 2500);
      BOOST_CHECK_EQUAL(sub_asset_4_obj.options.initial_max_supply.value, 2500);

      // Attempt to mint 55 subdivisions into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_4_id;
      mint_op.subdivisions = 55;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_4_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_obj = *token_itr;
      BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 55);
      BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 55);
      BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
      BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
      BOOST_CHECK(token_obj.current_backing == asset(0, core_id));
      BOOST_REQUIRE_EQUAL(sub_asset_4_dd.current_supply.value, 55);

      // Attempt to mint 30 subdivisions more into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_4_id;
      mint_op.subdivisions = 30;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 85);
      BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 85);
      BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
      BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
      BOOST_CHECK(token_obj.current_backing == asset(0, core_id));
      BOOST_REQUIRE_EQUAL(sub_asset_4_dd.current_supply.value, 85);

      // Attempt to mint 20 more subdivisions into Inventory
      // which should exceed the permissible quantity of 100 subdivisions for the token
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_4_id;
      mint_op.subdivisions = 20;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "single whole token is prohibited");

      BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 85);
      BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 85);
      BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
      BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
      BOOST_CHECK(token_obj.current_backing == asset(0, core_id));
      BOOST_REQUIRE_EQUAL(sub_asset_4_dd.current_supply.value, 85);

      // Attempt to mint more subdivisions with a different required backing than previously defined
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_4_id;
      mint_op.subdivisions = 5;
      mint_op.min_price_per_subdivision = asset(20, core_id);
      mint_op.req_backing_per_subdivision = asset(5, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Changing the required backing of a previously minted token is prohibited");

      // Attempt to mint more subdivisions with a different minimum price than previously defined
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_4_id;
      mint_op.subdivisions = 5;
      mint_op.min_price_per_subdivision = asset(20, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Changing the minimum price of a previously minted token is prohibited");

      // Attempt to mint the maximum possible subdivisions (15) into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_4_id;
      mint_op.subdivisions = 15;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 100);
      BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 100);
      BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
      BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
      BOOST_CHECK(token_obj.current_backing == asset(0, core_id));
      BOOST_REQUIRE_EQUAL(sub_asset_4_dd.current_supply.value, 100);

      // Attempt to mint 1 more subdivision into Inventory
      // which should exceed the permissible quantity of 100 subdivisions for the token
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_4_id;
      mint_op.subdivisions = 1;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "single whole token is prohibited");

      BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 100);
      BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 100);
      BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
      BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
      BOOST_CHECK(token_obj.current_backing == asset(0, core_id));
      BOOST_REQUIRE_EQUAL(sub_asset_4_dd.current_supply.value, 100);


      ///
      /// Test rejection of minting of a liquidity pool asset
      ///
      // Initialize
      const asset_object& usd = create_user_issued_asset("MYUSD");
      int64_t init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      issue_uia(alice, usd.amount(init_amount));
      const asset_object& lpa1 = create_user_issued_asset("SERIESA.LP1", alice, charge_market_fee );
      create_liquidity_pool(alice_id, core_id, usd.id, lpa1.id, 0, 0);
      BOOST_CHECK(lpa1.is_liquidity_pool_share_asset());

      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = lpa1.id;
      mint_op.subdivisions = 100;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "may not be a liquidity pool asset");


      ///
      /// Test rejection of minting of an MPA
      ///
      const asset_object& mpa = create_bitasset("SERIESA.MPA1", alice_id);
      BOOST_CHECK(mpa.is_market_issued());

      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = mpa.id;
      mint_op.subdivisions = 100;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "may not be a market-issued asset");


      ///
      /// Test rejection of minting of an asset that is prohibited from an increase in supply
      ///
      // Create the sub-asset
      const string sub_asset_5_name = series_name + ".SUB5";
      const uint16_t flags = DEFAULT_UIA_ASSET_ISSUER_PERMISSION | disable_new_supply;
      const asset_object &sub_asset_5_obj = create_user_issued_asset(sub_asset_5_name, alice_id(db), flags, 2500, 2);
      const asset_id_type sub_asset_5_id = sub_asset_5_obj.id;
      const asset_dynamic_data_object sub_asset_5_dd = sub_asset_5_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(sub_asset_5_obj.precision, 2);
      BOOST_CHECK_EQUAL(sub_asset_5_dd.current_max_supply.value, 2500);
      BOOST_CHECK_EQUAL(sub_asset_5_obj.options.initial_max_supply.value, 2500);

      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_5_id;
      mint_op.subdivisions = 100;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "prohibited from an increase in supply");


      ///
      /// Test rejection of minting of a NEW sub-asset which already has a pre-existing supply.
      /// Such a minting should be rejected because its existing supply
      /// is not being tracked through a Series Inventory.
      ///
      // Create the sub-asset
      const string sub_asset_6_name = series_name + ".SUB6";
      const asset_object& sub_asset_6_obj = create_user_issued_asset(sub_asset_6_name, alice_id(db), 0, 2500, 2);
      const asset_id_type sub_asset_6_id = sub_asset_6_obj.id;

      // Issue an amount into existence
      asset_issue_operation issue_op;
      issue_op.issuer = alice_id;
      issue_op.asset_to_issue =  sub_asset_6_obj.amount(5);
      issue_op.issue_to_account = bob_id;
      trx.clear();
      trx.operations.push_back(issue_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Attempt to mint the token into Inventory
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_6_id;
      mint_op.subdivisions = 20;
      mint_op.min_price_per_subdivision = asset(0, core_id);
      mint_op.req_backing_per_subdivision = asset(0, core_id);
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "should have no supply already in existence");

   } FC_LOG_AND_RETHROW()
}


/**
 * Test the rejection of minting prior to the hardfork
 */
BOOST_AUTO_TEST_CASE( nft_minting_before_hardfork ) {
   try {
      // Initialize
      ACTORS((alice));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, alice_id, graphene::chain::asset(init_balance));
      const asset_id_type core_id = asset_id_type();
      graphene::chain::nft_mint_operation mint_op;

      // Advance to before the hardfork time
      generate_blocks(HARDFORK_NFT_M1_TIME - 100);

      // Alice creates an asset
      const string series_name = "SERIESA";
      create_user_issued_asset(series_name, alice_id(db), 0);

      // Alice creates a sub-asset
      const string sub_asset_1_name = "SERIESA.SUB1";
      const asset_object &sub_asset_1 = create_user_issued_asset(sub_asset_1_name, alice_id(db), 0, 1000000, 2);
      const asset_id_type sub_asset_1_id = sub_asset_1.id;

      // Reject minting before the hardfork
      // Alice attempts a minting with Alice's asset
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = sub_asset_1_id;
      mint_op.subdivisions = 100; // An entire single token (10^precision = 10^2 = 100)
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "not yet enabled");

      // Reject minting in a proposal before the hardfork
      {
         proposal_create_operation pop;
         pop.review_period_seconds = 86400;
         uint32_t buffer_seconds = 60 * 60;
         pop.expiration_time = db.head_block_time() + *pop.review_period_seconds + buffer_seconds;
         pop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         pop.proposed_ops.emplace_back(mint_op);

         trx.clear();
         trx.operations.push_back(pop);
         // sign(trx, alice_private_key);
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Not allowed until");
      }
   } FC_LOG_AND_RETHROW()
}

/**
 * Test a simple and valid primary transfer of an NFT token
 */
BOOST_AUTO_TEST_CASE(nft_primary_transfer_a) {
   try {
      INVOKE(nft_mint_a);

      // Initialize
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      ACTOR(charlie);
      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;
      const asset_id_type core_id = asset_id_type();
      graphene::chain::nft_primary_transfer_operation ptx_op;

      ///
      /// Primary Transfer of token #1 which:
      /// - has 100 subdivision in a whole unit
      /// - requires no backing for extraction from Inventory
      ///
      // Verify the Inventory's balance before the primary transfer
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory BEFORE primary transfer of Token #1");
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_0 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_0.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_0.amount_in_inventory.value, 100);
      BOOST_REQUIRE_EQUAL(t1_0.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_0.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_0.current_backing == asset(0, core_id));
      // Verify everyone's balances before the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 0);

      // Alice attempts to primary transfer 40 subdivisions (40%) of the token from the Inventory
      BOOST_TEST_MESSAGE("Primary transfer of 40% of Token #1");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(40, sub_asset_1_id);
      ptx_op.to = charlie_id;
      ptx_op.manager = alice_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Verify the Inventory's balance after the primary transfer
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory AFTER primary transfer of Token #1");
      token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60); // Reduced by 40 subdivisions
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(0, core_id));
      // Verify everyone's balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40);

      ///
      /// Primary Transfer of token #2 which:
      /// - has 1000 subdivision in a whole unit
      /// - requires 500 core subdivisions for every subdivision of NFT to be extracted from Inventory
      ///
      // Verify the Inventory's balance before the primary transfer
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory BEFORE Primary Transfer #1 of Token #2");
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t2_0 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_0.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_0.amount_in_inventory.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_0.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_0.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_0.current_backing == asset(0, core_id));
      // Verify everyone's balances before the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 0);

      // Alice attempts to primary transfer 400 subdivisions (40%) of the token from the Inventory
      // provisioned by Alice
      BOOST_TEST_MESSAGE("Primary transfer of 40% of Token #2");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(400, sub_asset_2_id);
      ptx_op.to = charlie_id;
      ptx_op.manager = alice_id;
      ptx_op.provisioner = alice_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx);

      // Verify the Inventory's balance after the primary transfer
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory AFTER Primary Transfer #1 of Token #2");
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_1.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_1.amount_in_inventory.value, 600); // After extraction of 40%
      BOOST_REQUIRE_EQUAL(t2_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_1.current_backing == asset(200000, core_id)); // (400 NFT) * (500 CORE / NFT)
      // Verify everyone's balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 400);

      // Alice attempts to primary transfer 350 subdivisions (35%) of the token from the Inventory
      // provisioned by Bob
      BOOST_TEST_MESSAGE("Primary transfer of 35% of Token #2");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(350, sub_asset_2_id);
      ptx_op.to = charlie_id;
      ptx_op.manager = alice_id;
      ptx_op.provisioner = bob_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, alice_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx), tx_missing_active_auth ); // Should be missing Bob's signature

      // Add Bob's signature
      sign(trx, bob_private_key);
      PUSH_TX(db, trx); // Should proceed without any problem

      // Verify the Inventory's balance after the primary transfer
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory AFTER Primary Transfer #2 of Token #2");
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_2 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_2.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_2.amount_in_inventory.value, 250); // After extraction of 75%
      BOOST_REQUIRE_EQUAL(t2_2.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_2.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_2.current_backing == asset(375000, core_id)); // (750 NFT) * (500 CORE / NFT)
      // Verify everyone's balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 750);

      // Alice attempts to primary transfer 250 subdivisions (25%) of the token from the Inventory
      // provisioned by the GRAPHENE_TEMP_ACCOUNT
      // Initial attempt (Transfer #3) will attempt without being funded
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);

      BOOST_TEST_MESSAGE("Primary transfer of 25% of Token #2 (Attempt #1)");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(250, sub_asset_2_id);
      ptx_op.to = charlie_id;
      ptx_op.manager = alice_id;
      ptx_op.provisioner = GRAPHENE_TEMP_ACCOUNT;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Insufficient Balance");

      // Fund the GRAPHENE_TEMP_ACCOUNT
      const asset& provision_25_percent = asset(125000, core_id); //  (250 NFT) * (500 CORE / NFT)
      transfer(committee_account, GRAPHENE_TEMP_ACCOUNT, provision_25_percent);
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 125000);
      // Attempt a primary transfer again
      BOOST_TEST_MESSAGE("Primary transfer of 25% of Token #2 (Attempt #2)");
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, alice_private_key);
      PUSH_TX(db, trx); // Should proceed without any problem

      // Verify the Inventory's balance after the primary transfer
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory AFTER Primary Transfer #3 of Token #2");
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_3 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_3.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_3.amount_in_inventory.value, 0); // After extraction of 100%
      BOOST_REQUIRE_EQUAL(t2_3.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_3.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_3.current_backing == asset(500000, core_id)); // (1000 NFT) * (500 CORE / NFT)
      // Verify everyone's balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000);
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, sub_asset_2_id), 0);

      // Alice attempts to primary transfer 1 subdivisions (0.1%) of the token from the Inventory
      // provisioned by Alice
      // The attempt should fail because the inventory should be empty
      BOOST_TEST_MESSAGE("Primary transfer of 1 subdivision of Token #2 from empty Inventory");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_2_id);
      ptx_op.to = charlie_id;
      ptx_op.manager = alice_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance");

      // Verify the Inventory's balance after the primary transfer
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory AFTER Rejected Primary Transfer #4 of Token #2");
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_4 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_4.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_4.amount_in_inventory.value, 0); // After extraction of 100%
      BOOST_REQUIRE_EQUAL(t2_4.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_4.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_4.current_backing == asset(500000, core_id)); // (1000 NFT) * (500 CORE / NFT)
      // Verify everyone's balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000);
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, sub_asset_2_id), 0);

      // Verify the Inventory's balance of Token #1 after the primary transfer
      // It should be unaffected by the activity around Token #2
      token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_4 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_4.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_4.amount_in_inventory.value, 60); // Reduced by 40 subdivisions
      BOOST_REQUIRE_EQUAL(t1_4.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_4.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_4.current_backing == asset(0, core_id));
      // Verify everyone's balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40);

   } FC_LOG_AND_RETHROW()
}

/**
 * Test a valid primary transfer of an NFT token where the minimum price is greater than the required backing
 * in an "immediate" transaction signed by the necessary parties
 */
BOOST_AUTO_TEST_CASE(nft_primary_transfer_b) {
   try {
      // Initialize
      advance_past_m1_hardfork();

      ACTORS((creator)(mgr)(beneficiary)(treasury)(doug));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));
      const asset_id_type core_id = asset_id_type();

      // Create a Series where creator is the Issuer and mgr is the Series Manager
      const string series_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 0;
      create_asset_and_series(series_name, creator_id, creator_private_key,
                                          beneficiary_id, mgr_id, royalty_fee_centipercent);

      // Create an asset and mint it into the Series
      const string sub_asset_1_name = "SERIESA.SUB1";
      asset req_backing_per_subdivision = asset(500, core_id);
      asset min_price_per_subdivision = asset(750, core_id);
      const uint16_t flags = DEFAULT_UIA_ASSET_ISSUER_PERMISSION; // Include transfer restrictions and whitelisting
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, 2,
                                                                     creator_id, creator_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision,
                                                                     flags);

      // Primary transfer
      // Manager attempts to primary transfer 40 subdivisions (40%) of the token from the Inventory
      graphene::chain::nft_primary_transfer_operation ptx_op;

      const share_type treasury_balance_0 = get_balance(treasury_id, core_id);

      BOOST_TEST_MESSAGE("Primary transfer of 40% of Token #1");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(40, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      PUSH_TX(db, trx);

      // Verify backing
      const share_type expected_required_backing = 20000; // (40 NFT subdivision) * (500 core / NFT subdivision)
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60); // = 100 - 40
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(expected_required_backing, core_id));

      // Verify payment by funding source
      // Interpretation #1 of the concept design requires enough payment
      // to cover the backing behind the amount of token extracted from the Inventory
      const share_type expected_treasury_balance_1 = treasury_balance_0 - expected_required_backing;
      const share_type treasury_balance_1 = get_balance(treasury_id, core_id);
      BOOST_CHECK(expected_treasury_balance_1 == treasury_balance_1);

      // Verify account balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 40);
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);

   } FC_LOG_AND_RETHROW()
}


/**
 * Test a valid primary transfer of an NFT token where the minimum price is greater than the required backing
 * with a two-operation proposal:
 * 1. the first operation will have a third-party send the required backing to GRAPHENE_TEMP_ACCOUNT
 * 2. the second operation will be the primary transfer that references the GRAPHENE_TEMP_ACCOUNT as a provisioner
 */
BOOST_AUTO_TEST_CASE(nft_primary_transfer_b_proposal_provisioned_by_GRAPHENE_TEMP_ACCOUNT) {
   try {
      // Initialize
      advance_past_m1_hardfork();

      ACTORS((creator)(mgr)(beneficiary)(treasury)(doug)(rando));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));
      transfer(committee_account, rando_id, graphene::chain::asset(init_balance));
      const asset_id_type core_id = asset_id_type();

      // Create a Series where creator is the Issuer and mgr is the Series Manager
      const string series_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 0;
      create_asset_and_series(series_name, creator_id, creator_private_key,
                              beneficiary_id, mgr_id, royalty_fee_centipercent);

      // Create an asset and mint it into the Series
      const string sub_asset_1_name = "SERIESA.SUB1";
      asset req_backing_per_subdivision = asset(500, core_id);
      asset min_price_per_subdivision = asset(750, core_id);
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, 2,
                                                                     creator_id, creator_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision);

      const share_type treasury_balance_0 = get_balance(treasury_id, core_id);

      // Prepare Operation #1: Transfer backing from the treasury to the GRAPHENE_TEMP_ACCOUNT
      const share_type expected_required_backing = 20000; // (40 NFT subdivision) * (500 core / NFT subdivision)
      transfer_operation tx_op;
      tx_op.from = treasury_id;
      tx_op.to = GRAPHENE_TEMP_ACCOUNT;
      tx_op.amount = asset(expected_required_backing, core_id);

      // Prepare Operation #2: NFT Primary transfer
      // Manager attempts to primary transfer 40 subdivisions (40%) of the token from the Inventory
      // funded by the GRAPHENE_TEMP_ACCOUNT
      graphene::chain::nft_primary_transfer_operation ptx_op;
      BOOST_TEST_MESSAGE("Submitting multi-op NFT proposal");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(40, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = GRAPHENE_TEMP_ACCOUNT;

      proposal_create_operation prop_create_op;
      prop_create_op.review_period_seconds = 86400;
      uint32_t buffer_seconds = 60 * 60;
      prop_create_op.expiration_time = db.head_block_time() + *prop_create_op.review_period_seconds + buffer_seconds;
      prop_create_op.proposed_ops.emplace_back(tx_op); // Emplace the transfer op
      prop_create_op.proposed_ops.emplace_back(ptx_op); // Emplace the NFT Primary Transfer op
      prop_create_op.fee_paying_account = rando_id; // Any account may submit a proposal

      trx.clear();
      trx.operations.push_back(prop_create_op);
      sign(trx, rando_private_key); // Any account may submit a proposal
      processed_transaction ptx = PUSH_TX(db, trx);
      const proposal_object& prop = db.get<proposal_object>(ptx.operation_results.front().get<object_id_type>());
      proposal_id_type prop_id = prop.id;

      // Verify that balances should not yet be affected because the proposal has not been approved nor matured
      BOOST_REQUIRE(get_balance(treasury_id, core_id) == treasury_balance_0);
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);

      // Treasury approves the proposal
      BOOST_TEST_MESSAGE("Treasury approves the multi-op NFT proposal");
      proposal_update_operation prop_update_op;
      prop_update_op.proposal = prop_id;
      prop_update_op.active_approvals_to_add = {treasury_id};
      prop_update_op.fee_paying_account = treasury_id;
      trx.clear();
      trx.operations.push_back(prop_update_op);
      sign(trx, treasury_private_key);
      PUSH_TX(db, trx);

      // Series Manager approves the proposal
      BOOST_TEST_MESSAGE("Manager approves the multi-op NFT proposal");
      prop_update_op = proposal_update_operation();
      prop_update_op.proposal = prop_id;
      prop_update_op.active_approvals_to_add = {mgr_id};
      prop_update_op.fee_paying_account = mgr_id;
      trx.clear();
      trx.operations.push_back(prop_update_op);
      sign(trx, mgr_private_key);
      PUSH_TX(db, trx);

      // Await for the proposal to mature
      generate_blocks(prop_id(db).expiration_time);
      generate_block();

      ///
      /// Verify the results of the successful primary transfer
      ///
      BOOST_TEST_MESSAGE("Verifying outcome of NFT proposal");
      // Verify backing
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60); // = 100 - 40
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(expected_required_backing, core_id));

      // Verify payment by funding source
      // Interpretation #1 of the concept design requires enough payment
      // to cover the backing behind the amount of token extracted from the Inventory
      const share_type expected_treasury_balance_1 = treasury_balance_0 - expected_required_backing;
      const share_type treasury_balance_1 = get_balance(treasury_id, core_id);
      BOOST_CHECK(expected_treasury_balance_1 == treasury_balance_1);
      BOOST_CHECK_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);

      // Verify account balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 40);

   } FC_LOG_AND_RETHROW()
}


/**
 * Test a valid primary transfer of an NFT token where the minimum price is greater than the required backing
 * with a single-operation proposal:
 * 1. a primary transfer that references a treasury account
 */
BOOST_AUTO_TEST_CASE(nft_primary_transfer_b_proposal_provisioned_by_treasury) {
   try {
      // Initialize
      advance_past_m1_hardfork();

      ACTORS((creator)(mgr)(beneficiary)(treasury)(doug)(rando));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));
      transfer(committee_account, rando_id, graphene::chain::asset(init_balance));
      const asset_id_type core_id = asset_id_type();

      // Create a Series where creator is the Issuer and mgr is the Series Manager
      const string series_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 0;
      create_asset_and_series(series_name, creator_id, creator_private_key,
                              beneficiary_id, mgr_id, royalty_fee_centipercent);

      // Create an asset and mint it into the Series
      const string sub_asset_1_name = "SERIESA.SUB1";
      asset req_backing_per_subdivision = asset(500, core_id);
      asset min_price_per_subdivision = asset(750, core_id);
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, 2,
                                                                     creator_id, creator_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision);

      const share_type treasury_balance_0 = get_balance(treasury_id, core_id);

      // Prepare Operation #1: NFT Primary transfer
      const share_type expected_required_backing = 20000; // (40 NFT subdivision) * (500 core / NFT subdivision)

      // Manager attempts to primary transfer 40 subdivisions (40%) of the token from the Inventory
      // funded by the treasury account
      graphene::chain::nft_primary_transfer_operation ptx_op;
      BOOST_TEST_MESSAGE("Submitting single-op NFT proposal");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(40, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;

      proposal_create_operation prop_create_op;
      prop_create_op.review_period_seconds = 86400;
      uint32_t buffer_seconds = 60 * 60;
      prop_create_op.expiration_time = db.head_block_time() + *prop_create_op.review_period_seconds + buffer_seconds;
      prop_create_op.proposed_ops.emplace_back(ptx_op); // Emplace the NFT Primary Transfer op
      prop_create_op.fee_paying_account = rando_id; // Any account may submit a proposal

      trx.clear();
      trx.operations.push_back(prop_create_op);
      sign(trx, rando_private_key); // Any account may submit a proposal
      processed_transaction ptx = PUSH_TX(db, trx);
      const proposal_object& prop = db.get<proposal_object>(ptx.operation_results.front().get<object_id_type>());
      proposal_id_type prop_id = prop.id;

      // Verify that balances should not yet be affected because the proposal has not been approved nor matured
      BOOST_REQUIRE(get_balance(treasury_id, core_id) == treasury_balance_0);

      // Treasury approves the proposal
      BOOST_TEST_MESSAGE("Treasury approves the single-op NFT proposal");
      proposal_update_operation prop_update_op;
      prop_update_op.proposal = prop_id;
      prop_update_op.active_approvals_to_add = {treasury_id};
      prop_update_op.fee_paying_account = treasury_id;
      trx.clear();
      trx.operations.push_back(prop_update_op);
      sign(trx, treasury_private_key);
      PUSH_TX(db, trx);

      // Series Manager approves the proposal
      BOOST_TEST_MESSAGE("Manager approves the single-op NFT proposal");
      prop_update_op = proposal_update_operation();
      prop_update_op.proposal = prop_id;
      prop_update_op.active_approvals_to_add = {mgr_id};
      prop_update_op.fee_paying_account = mgr_id;
      trx.clear();
      trx.operations.push_back(prop_update_op);
      sign(trx, mgr_private_key);
      PUSH_TX(db, trx);

      // Await for the proposal to mature
      generate_blocks(prop_id(db).expiration_time);
      generate_block();

      ///
      /// Verify the results of the successful primary transfer
      ///
      BOOST_TEST_MESSAGE("Verifying outcome of NFT proposal");
      // Verify backing
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60); // = 100 - 40
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(expected_required_backing, core_id));

      // Verify payment by funding source
      // Interpretation #1 of the concept design requires enough payment
      // to cover the backing behind the amount of token extracted from the Inventory
      const share_type expected_treasury_balance_1 = treasury_balance_0 - expected_required_backing;
      const share_type treasury_balance_1 = get_balance(treasury_id, core_id);
      BOOST_CHECK(expected_treasury_balance_1 == treasury_balance_1);

      // Verify account balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 40);

   } FC_LOG_AND_RETHROW()
}


/**
 * Test a valid primary transfer of an NFT token consisting of a SINGLE subdivision
 * where the minimum price is greater than the required backing
 * in an "immediate" transaction signed by the necessary parties
 */
BOOST_AUTO_TEST_CASE(nft_primary_transfer_c) {
   try {
      // Initialize
      advance_past_m1_hardfork();

      ACTORS((creator)(mgr)(beneficiary)(treasury)(doug));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));
      const asset_id_type core_id = asset_id_type();

      // Create a Series where creator is the Issuer and mgr is the Series Manager
      const string series_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 0;
      create_asset_and_series(series_name, creator_id, creator_private_key,
                                          beneficiary_id, mgr_id, royalty_fee_centipercent);

      // Create an asset and mint it into the Series
      const string sub_asset_1_name = "SERIESA.SUB1";
      asset req_backing_per_subdivision = asset(500, core_id);
      asset min_price_per_subdivision = asset(750, core_id);
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, 0,
                                                                     creator_id, creator_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision);

      // Primary transfer
      // Manager attempts to primary transfer 1 subdivisions (100%) of the token from the Inventory
      graphene::chain::nft_primary_transfer_operation ptx_op;

      const share_type treasury_balance_0 = get_balance(treasury_id, core_id);

      BOOST_TEST_MESSAGE("Primary transfer of 100% of Token #1");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      PUSH_TX(db, trx);

      // Verify backing
      const share_type expected_required_backing = 500; // (1 NFT subdivision) * (500 core / NFT subdivision)
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 1);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 0); // = 1 - 1
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(expected_required_backing, core_id));

      // Verify payment by funding source
      // Interpretation #1 of the concept design requires enough payment
      // to cover the backing behind the amount of token extracted from the Inventory
      const share_type expected_treasury_balance_1 = treasury_balance_0 - expected_required_backing;
      const share_type treasury_balance_1 = get_balance(treasury_id, core_id);
      BOOST_CHECK(expected_treasury_balance_1 == treasury_balance_1);

      // Verify account balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 1);

      ///
      /// Attempt additional Primary transfer which should fail because the Inventory should be empty
      ///
      // Advance to the next block
      generate_block();
      trx.clear();
      set_expiration(db, trx);

      // Check that a duplicate attempt fails
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance");

   } FC_LOG_AND_RETHROW()
}


/**
 * Test the rejection of a primary transfer of backed token due to invalid parameters
 */
BOOST_AUTO_TEST_CASE(nft_primary_transfer_backed_token_invalid) {
   try {
      // Initialize
      advance_past_m1_hardfork();

      ACTORS((creator)(mgr)(beneficiary)(treasury)(doug)(rando)(fundedrando)(izzy));
      upgrade_to_lifetime_member( izzy_id ); // <-- Izzy will need to be a LTM to handle whitelisting duties
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));
      transfer(committee_account, fundedrando_id, graphene::chain::asset(init_balance));
      const asset_id_type core_id = asset_id_type();

      // Create a Series where creator is the Issuer and mgr is the Series Manager
      const string series_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 0;
      create_asset_and_series(series_name, creator_id, creator_private_key,
                              beneficiary_id, mgr_id, royalty_fee_centipercent);

      // Create an asset and mint it into the Series
      const string sub_asset_1_name = "SERIESA.SUB1";
      const uint8_t sub_asset_precision = 2;
      asset req_backing_per_subdivision = asset(500, core_id);
      asset min_price_per_subdivision = asset(750, core_id);
      const uint16_t flags = DEFAULT_UIA_ASSET_ISSUER_PERMISSION; // Include whitelisting
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, sub_asset_precision,
                                                                     creator_id, creator_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision,
                                                                     flags);

      // Create an asset that is NOT added to the inventory
      const string wild_sub_asset_name = "SERIESA.WILD";
      const uint64_t& whole_token_subdivisions = asset::scaled_precision(sub_asset_precision).value;
      const uint64_t& max_supply = whole_token_subdivisions;
      const asset_object& wild_sub_asset_obj = create_user_issued_asset(wild_sub_asset_name,
                                                                        creator_id(db),
                                                                        flags,
                                                                        max_supply,
                                                                        sub_asset_precision);
      const asset_id_type wild_sub_asset_id = wild_sub_asset_obj.id;

      // Primary transfer
      // Manager attempts to primary transfer 1 subdivisions (100%) of the token from the Inventory
      graphene::chain::nft_primary_transfer_operation ptx_op;

      // Attempt to Primary Transfer without indicating a provisioner
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to Primary Transfer without the signature of the manager nor treasury
      // but signed only by a third-party (rando)
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = mgr_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, rando_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to Primary Transfer with the signature of the manager but missing that of the treasury
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to Primary Transfer with the signature of the treasury but missing that of the manager
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, treasury_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to Primary Transfer with the signature of the manager but missing that of the treasury
      // and signed by a random third-party
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, rando_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to Primary Transfer with the signature of the treasury but missing that of the manager
      // and signed by a random third-party
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, treasury_private_key);
      sign(trx, rando_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to Primary Transfer without the signature of both the treasury and manager
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = fundedrando_id;
      ptx_op.provisioner = fundedrando_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, fundedrando_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "may only be initiated by the Series Manager");

      // Attempt to construct a Primary Transfer involving zero amount
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(0, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      REQUIRE_EXCEPTION_WITH_TEXT(ptx_op.validate(), "should be positive");

      // Attempt to construct a Primary Transfer involving a negative amount
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(-5, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      REQUIRE_EXCEPTION_WITH_TEXT(ptx_op.validate(), "should be positive");

      // Attempt to Primary Transfer involving an asset not in the Inventory
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, wild_sub_asset_id); // <-- Not associated with the Inventory
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Attempt to Primary Transfer funded by a provisioner
      // that has insufficient balances to back the token
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = doug_id; // <-- Has insufficient funds to support provisioning
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, doug_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Insufficient Balance");

      ///
      /// Test Primary Transfer under the presence of authorized and prohibited lists for a sub-asset
      ///
      // Create a whitelist
      BOOST_TEST_MESSAGE( "Creating a whitelist authority for an sub-asset in the Series" );
      asset_update_operation uop;
      uop.issuer = creator_id;
      uop.asset_to_update = sub_asset_1_id;
      uop.new_options = sub_asset_1_id(db).options;
      uop.new_options.whitelist_authorities.insert(izzy_id);
      trx.operations.back() = uop;
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK(sub_asset_1_id(db).options.whitelist_authorities.find(izzy_id) != sub_asset_1_id(db).options.whitelist_authorities.end());

      // Attempt to Primary Transfer funded by a provisioner
      // that has insufficient balances to back the token
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "is not whitelisted for asset");

      // Authorize the sub-asset's issuer
      account_whitelist_operation wop;
      wop.authorizing_account = izzy_id;
      wop.account_to_list = creator_id;
      wop.new_listing = account_whitelist_operation::white_listed;
      trx.clear();
      trx.operations.push_back(wop);
      sign(trx, izzy_private_key);
      PUSH_TX(db, trx);

      // Attempt to Primary Transfer again.
      // The attempt should fail because, although the sender is authorized,
      // the recipient is not yet authorized
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "is not whitelisted for asset");

      // Authorize the intended recipient of the Primary Transfer
      wop = account_whitelist_operation();
      wop.authorizing_account = izzy_id;
      wop.account_to_list = doug_id;
      wop.new_listing = account_whitelist_operation::white_listed;
      trx.clear();
      trx.operations.push_back(wop);
      sign(trx, izzy_private_key);
      PUSH_TX(db, trx);

      // Attempt to Primary Transfer again.
      // The attempt should succeed because the sender and recipient are whitelisted
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      PUSH_TX(db, trx);

   } FC_LOG_AND_RETHROW()
}


/**
 * Test the rejection of Primary Transfers prior to the hardfork
 */
BOOST_AUTO_TEST_CASE(nft_primary_transfer_before_hardfork) {
   try {
      // Initialize
      ACTORS((creator)(mgr)(treasury)(doug));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));

      // Advance to before the hardfork time
      generate_blocks(HARDFORK_NFT_M1_TIME - 100);

      // Creator creates an asset
      const string series_name = "SERIESA";
      create_user_issued_asset(series_name, creator_id(db), 0);

      // Creator creates a sub-asset
      const string sub_asset_1_name = "SERIESA.SUB1";
      const asset_object &sub_asset_1 = create_user_issued_asset(sub_asset_1_name, creator_id(db), 0, 1000000, 2);
      const asset_id_type sub_asset_1_id = sub_asset_1.id;

      // Reject Primary Transfer before the hardfork
      nft_primary_transfer_operation ptx_op;
      ptx_op.amount = asset(1, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "not yet enabled");

      // Reject Primary Transfer in a proposal before the hardfork
      {
         proposal_create_operation pop;
         pop.review_period_seconds = 86400;
         uint32_t buffer_seconds = 60 * 60;
         pop.expiration_time = db.head_block_time() + *pop.review_period_seconds + buffer_seconds;
         pop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         pop.proposed_ops.emplace_back(ptx_op);

         trx.clear();
         trx.operations.push_back(pop);
         // sign(trx, creator_private_key); // No signature required for operations paid by GRAPHENE_TEMP_ACCOUNT
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Not allowed until");
      }
   } FC_LOG_AND_RETHROW()
}


/**
 * Test the complete returns of NFT tokens
 * - Token #1 is un-backed
 * - Token #2 is backed
 */
BOOST_AUTO_TEST_CASE(nft_return_of_complete_returns_a) {
   try {
      INVOKE(nft_primary_transfer_a);
      advance_past_m2_hardfork();

      // Initialize
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      share_type alice_init_balance_core = get_balance(alice_id, core_id);
      share_type bob_init_balance_core = get_balance(bob_id, core_id);
      share_type charlie_init_balance_core = get_balance(charlie_id, core_id);

      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;
      graphene::chain::nft_return_operation return_op;


      ///
      /// Return of token #1 which:
      /// - has 100 subdivision in a whole unit
      /// - requires no backing for extraction from Inventory
      ///
      // Verify the Inventory's balance after the return
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60);
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(0, core_id));
      // Verify everyone's balances after the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40);
      // Core balances should remain unchanged
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);

      // Charlie attempts to return 41 subdivisions when he only own 40 subdivisions
      BOOST_TEST_MESSAGE("Return #1: Invalid return of 41% of Token #1");
      const share_type t1_return1 = 41;
      return_op = nft_return_operation();
      return_op.amount = asset(t1_return1, sub_asset_1_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the bearer's available balance");

      // Charlie attempts to return 40 of his 40 subdivisions (40%) of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #2: Return of 40% of Token #1");
      const share_type t1_return2 = 40;
      return_op = nft_return_operation();
      return_op.amount = asset(t1_return2, sub_asset_1_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      PUSH_TX(db, trx);

      // Verify the Inventory's balance after the return
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60 + t1_return2.value);
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(0, core_id));
      // Verify everyone's balances after the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40 - t1_return2.value);
      // Core balances should remain unchanged
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);

      ///
      /// Charlie attempts to return 1 subdivisions (1%) of the token to the Inventory DESPITE having none remaining
      ///
      BOOST_TEST_MESSAGE("Return #3: Invalid return of 1% of Token #1");
      const share_type return3 = 1;
      return_op = nft_return_operation();
      return_op.amount = asset(return3, sub_asset_1_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the bearer's available balance");


      ///
      /// Return of token #2 which:
      /// - has 1000 subdivision in a whole unit
      /// - is backed by 500 core subdivisions for every subdivision of NFT
      ///
      // Verify the Inventory's balance before the return
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory BEFORE Return #1 of Token #2");
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t2_0 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_0.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_0.amount_in_inventory.value, 0); // After extraction of 100%
      BOOST_REQUIRE_EQUAL(t2_0.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_0.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_0.current_backing == asset(500000, core_id)); // (1000 NFT) * (500 CORE / NFT)
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000);
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);

      // Charlie attempts to return 1001 of his 1000 subdivisions (100.1%) of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #1: Invalid return of 100.1% of Token #2");
      const share_type t2_return1 = 1001;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return1, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the bearer's available balance");

      // Verify the Inventory's balance after the failed return
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_1.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_1.amount_in_inventory.value, 0); // After return of 100%
      BOOST_REQUIRE_EQUAL(t2_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_1.current_backing == asset(500000, core_id)); // (1000 NFT) * (500 CORE / NFT)
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000);
      // Core balances should remain unchanged
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);

      // Charlie attempts to return 1000 of his 1000 subdivisions (100.0%) of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #2: Return of 100% of Token #2");
      const share_type t2_return2 = 1000;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return2, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      PUSH_TX(db, trx);

      // Verify the Inventory's balance after the return
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_2 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_2.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_2.amount_in_inventory.value, 1000); // After return of 1000 subdivisions
      BOOST_REQUIRE_EQUAL(t2_2.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_2.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_2.current_backing == (asset(500000, core_id) - asset(500000, core_id)) ); // Reduced by redemption
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 0); // After return of 1000 subdivisions
      // Core balances should remain unchanged EXCEPT for Charlie's
      // which are increased due to the redemption
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id),
                          charlie_init_balance_core.value + 500000); // (1000 NFT) * (500 CORE / NFT)

      ///
      /// Charlie attempts to return 1 subdivisions (1%) of the token to the Inventory DESPITE having none remaining
      ///
      BOOST_TEST_MESSAGE("Return #3: Invalid return of 1% of Token #2");
      const share_type t2_return3 = 1;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return3, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the bearer's available balance");

   } FC_LOG_AND_RETHROW()
}


/**
 * Test partial returns of NFT tokens
 * - Token #1 is un-backed
 * - Token #2 is backed
 */
BOOST_AUTO_TEST_CASE(nft_return_of_partial_returns_a) {
   try {
      INVOKE(nft_primary_transfer_a);
      advance_past_m2_hardfork();

      // Initialize
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      share_type alice_init_balance_core = get_balance(alice_id, core_id);
      share_type bob_init_balance_core = get_balance(bob_id, core_id);
      share_type charlie_init_balance_core = get_balance(charlie_id, core_id);

      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;
      graphene::chain::nft_return_operation return_op;


      ///
      /// Return of token #1 which:
      /// - has 100 subdivision in a whole unit
      /// - requires no backing for extraction from Inventory
      ///
      // Verify the Inventory's balance after the return
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60);
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(0, core_id));
      // Verify everyone's balances after the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40);
      // Core balances should remain unchanged
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);

      // Charlie attempts to return 10 of his 40 subdivisions (10%) of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #1: Return of 10% of Token #1");
      const share_type return1 = 10;
      return_op = nft_return_operation();
      return_op.amount = asset(return1, sub_asset_1_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      PUSH_TX(db, trx);

      // Verify the Inventory's balance after the return
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60 + return1.value);
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(0, core_id));
      // Verify everyone's balances after the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40 - return1.value);
      // Core balances should remain unchanged
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);


      ///
      // Charlie attempts to return 30 of his 30 subdivisions (30%) of the token to the Inventory
      ///
      BOOST_TEST_MESSAGE("Return #2: Return of 30% of Token #1");
      const share_type return2 = 30;
      return_op = nft_return_operation();
      return_op.amount = asset(return2, sub_asset_1_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      PUSH_TX(db, trx);

      // Verify the Inventory's balance after the return
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 60 + return1.value + return2.value);
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(0, core_id));
      // Verify everyone's balances after the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40 - return1.value - return2.value);
      // Core balances should remain unchanged
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);


      ///
      // Charlie attempts to return 1 subdivisions (1%) of the token to the Inventory DESPITE having none remaining
      ///
      BOOST_TEST_MESSAGE("Return #3: Invalid return of 1% of Token #1");
      const share_type return3 = 1;
      return_op = nft_return_operation();
      return_op.amount = asset(return3, sub_asset_1_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the bearer's available balance");


      ///
      /// Return of token #2 which:
      /// - has 1000 subdivision in a whole unit
      /// - is backed by 500 core subdivisions for every subdivision of NFT
      ///
      // Verify the Inventory's balance before the return
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory BEFORE Return #1 of Token #2");
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t2_0 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_0.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_0.amount_in_inventory.value, 0); // After extraction of 100%
      BOOST_REQUIRE_EQUAL(t2_0.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_0.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_0.current_backing == asset(500000, core_id)); // (1000 NFT) * (500 CORE / NFT)
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000);
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);

      // Charlie attempts to return 1001 of his 1000 subdivisions (100.1%) of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #1: Invalid return of 100.1% of Token #2");
      const share_type t2_return1 = 1001;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return1, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the bearer's available balance");

      // Verify the Inventory's balance after the failed return
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_1.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_1.amount_in_inventory.value, 0); // After return of 100%
      BOOST_REQUIRE_EQUAL(t2_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_1.current_backing == asset(500000, core_id)); // (1000 NFT) * (500 CORE / NFT)
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000);
      // Core balances should remain unchanged
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);

      // Charlie attempts to return 600 of the 1000 subdivisions (60%) of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #2: Return of 60% of Token #2");
      const share_type t2_return2 = 600;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return2, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      PUSH_TX(db, trx);
      // Verify the Inventory's balance after the return
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_2 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_2.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_2.amount_in_inventory.value, 0 + 600); // After return of 600 subdivisions
      BOOST_REQUIRE_EQUAL(t2_2.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_2.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_2.current_backing == (asset(500000, core_id)
                                             - asset(300000, core_id)) ); // Reduced by redemption of (600 NFT) * (500 CORE / NFT)
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000 - 600); // After return of 600 subdivisions
      // Core balances should remain unchanged EXCEPT for Charlie's
      // which are increased due to the redemption
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id),
                          charlie_init_balance_core.value + 300000); // (600 NFT) * (500 CORE / NFT)

      // Charlie attempts to return 300 of the 1000 subdivisions (30%) of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #3: Return of 30% of Token #2");
      const share_type t2_return3 = 300;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return3, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      PUSH_TX(db, trx);
      // Verify the Inventory's balance after the return
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_3 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_3.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_3.amount_in_inventory.value, 0 + 600 + 300); // After return of 900 subdivisions
      BOOST_REQUIRE_EQUAL(t2_3.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_3.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_3.current_backing == (asset(500000, core_id)
                                             - asset(300000, core_id)
                                             - asset(150000, core_id)) ); // Reduced by redemption of (900 NFT) * (500 CORE / NFT)
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000 - 600 - 300); // After return of 900 subdivisions
      // Core balances should remain unchanged EXCEPT for Charlie's
      // which are increased due to the redemption
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id),
                          charlie_init_balance_core.value + 300000 + 150000); // (900 NFT) * (500 CORE / NFT)

      // Charlie attempts to return 101 of his 100 subdivisions of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #4: Invalid return of Token #2 due to insufficient balance");
      const share_type t2_return4 = 101;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return4, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the bearer's available balance");

      // Charlie attempts to return 100 of the 1000 subdivisions (10%) of the token to the Inventory
      BOOST_TEST_MESSAGE("Return #5: Return of 10% of Token #2");
      const share_type t2_return5 = 100;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return5, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      PUSH_TX(db, trx);
      // Verify the Inventory's balance after the return
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &t2_5 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_5.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_5.amount_in_inventory.value, 0 + 600 + 300 + 100); // After return of 1000 subdivisions
      BOOST_REQUIRE_EQUAL(t2_5.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_5.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_5.current_backing == (asset(500000, core_id)
                                             - asset(300000, core_id)
                                             - asset(150000, core_id)
                                             - asset(50000, core_id)) ); // Reduced by redemption of (1000 NFT) * (500 CORE / NFT)
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000 - 600 - 300 - 100); // After return of 1000 subdivisions
      // Core balances should remain unchanged EXCEPT for Charlie's
      // which are increased due to the redemption
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id),
                          charlie_init_balance_core.value + 300000 + 150000 + 50000); // (1000 NFT) * (500 CORE / NFT)

      ///
      /// Charlie attempts to return 1 subdivisions of the token to the Inventory DESPITE having none remaining
      ///
      BOOST_TEST_MESSAGE("Return #6: Invalid return of Token #2 due to insufficient balance");
      const share_type t2_return6 = 1;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return6, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the bearer's available balance");

   } FC_LOG_AND_RETHROW()
}


/**
 * Test invalid returns of NFT tokens
 * - Token #1 is un-backed
 * - Token #2 is backed
 */
BOOST_AUTO_TEST_CASE(nft_invalid_returns_a) {
   try {
      INVOKE(nft_primary_transfer_a);
      advance_past_m2_hardfork();

      // Initialize
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      share_type alice_init_balance_core = get_balance(alice_id, core_id);
      share_type bob_init_balance_core = get_balance(bob_id, core_id);
      share_type charlie_init_balance_core = get_balance(charlie_id, core_id);

      const string series_name = "SERIESA";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;
      graphene::chain::nft_return_operation return_op;

      ///
      /// Return of token #2 which:
      /// - has 1000 subdivision in a whole unit
      /// - is backed by 500 core subdivisions for every subdivision of NFT
      ///
      // Verify the Inventory's balance before the return
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory BEFORE checking invalid returns of Token #2");
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t2_0 = *token_itr;
      BOOST_REQUIRE_EQUAL(t2_0.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(t2_0.amount_in_inventory.value, 0); // After extraction of 100%
      BOOST_REQUIRE_EQUAL(t2_0.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t2_0.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t2_0.current_backing == asset(500000, core_id)); // (1000 NFT) * (500 CORE / NFT)
      // Verify everyone's balances before the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000);
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);

      BOOST_TEST_MESSAGE("Testing invalid returns of Token #2");

      // Bob attempts to return Charlie's subdivisions to the Inventory
      const share_type t2_return1 = 1000;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return1, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, bob_private_key); // Should be signed by the bearer
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Charlie attempts to return zero/no subdivisions to the Inventory
      const share_type t2_return2A = 0;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return2A, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Should be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a return should be positive");

      // Charlie attempts to return negative subdivisions to the Inventory
      const share_type t2_return2B = -5;
      return_op = nft_return_operation();
      return_op.amount = asset(t2_return2B, sub_asset_2_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Should be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a return should be positive");

      ///
      /// Attempt to return an asset that is NOT an NFT despite being a sub-asset of the series.
      /// Note that the sub-asset has NOT been associated with the series through a minting process.
      ///
      BOOST_TEST_MESSAGE("Attempting an invalid return of a sub-asset that is NOT an NFT");
      // Create the sub-asset
      const string sub_asset_3_name = series_name + ".WILD";
      const asset_object &sub_asset_3_obj = create_user_issued_asset(sub_asset_3_name, alice_id(db), 0, 1000000, 2);
      const asset_id_type sub_asset_3_id = sub_asset_3_obj.id;
      const asset_dynamic_data_object sub_asset_3_dd = sub_asset_3_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(sub_asset_3_obj.precision, 2);
      BOOST_CHECK_EQUAL(sub_asset_3_dd.current_max_supply.value, 1000000);
      BOOST_CHECK_EQUAL(sub_asset_3_obj.options.initial_max_supply.value, 1000000);

      // Issue an amount to charlie
      issue_uia(charlie_id, graphene::chain::asset(12345, sub_asset_3_id));

      // Have charlie attempt to return them
      const share_type t3_return1 = 1;
      return_op = nft_return_operation();
      return_op.amount = asset(t3_return1, sub_asset_3_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Should be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Returns may only be performed for NFT tokens");

      ///
      /// Attempt to return an asset that is NOT an NFT nor a sub-asset of the series
      ///
      BOOST_TEST_MESSAGE("Attempting an invalid return of an asset that is NOT an NFT");
      // Create the sub-asset
      const string asset_4_name = "RANDO";
      const asset_object &asset_4_obj = create_user_issued_asset(asset_4_name, alice_id(db), 0, 1000000, 2);
      const asset_id_type asset_4_id = asset_4_obj.id;
      const asset_dynamic_data_object asset_4_dd = asset_4_obj.dynamic_data(db);
      BOOST_CHECK_EQUAL(asset_4_obj.precision, 2);
      BOOST_CHECK_EQUAL(asset_4_dd.current_max_supply.value, 1000000);
      BOOST_CHECK_EQUAL(asset_4_obj.options.initial_max_supply.value, 1000000);

      // Issue an amount to charlie
      issue_uia(charlie_id, graphene::chain::asset(67890, asset_4_id));

      // Have charlie attempt to return them
      const share_type t4_return1 = 1;
      return_op = nft_return_operation();
      return_op.amount = asset(t4_return1, asset_4_id);
      return_op.bearer = charlie_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, charlie_private_key); // Should be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Returns may only be performed for NFT tokens");

   } FC_LOG_AND_RETHROW()
}

/**
 * Test invalid returns of NFT tokens
 * - Token #1 is backed, and has permissions and flags for testing of whitelisting and transfer-restriction
 */
BOOST_AUTO_TEST_CASE(nft_invalid_returns_b) {
   try {
      // Scenario B contains a token that is already configured with whitelisting and transfer-restriction
      INVOKE(nft_primary_transfer_b);
      advance_past_m2_hardfork();

      // Initialize
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(creator);
      GET_ACTOR(mgr);
      GET_ACTOR(beneficiary);
      GET_ACTOR(treasury);
      GET_ACTOR(doug);

      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      graphene::chain::nft_return_operation return_op;

      // Verify the Inventory's balance before the return
      BOOST_TEST_MESSAGE("");
      BOOST_TEST_MESSAGE("Scenario B");
      BOOST_TEST_MESSAGE("Verifying the state of Series Inventory BEFORE checking invalid returns of Token #1");
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_0 = *token_itr;

      // Verify backing
      const share_type expected_required_backing = 20000; // (40 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_REQUIRE_EQUAL(t1_0.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_0.amount_in_inventory.value, 60); // = 100 - 40
      BOOST_REQUIRE_EQUAL(t1_0.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_0.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_0.current_backing == asset(expected_required_backing, core_id));

      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 40);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, core_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);

      ///
      /// Test transfer restriction
      ///
      // Doug attempts to return 1 subdivision of the token
      const share_type t1_return1 = 1;
      return_op = nft_return_operation();
      return_op.amount = asset(t1_return1, sub_asset_1_id);
      return_op.bearer = doug_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, doug_private_key); // Should be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "has transfer_restricted flag enabled");

      // Disable the transfer restriction
      asset_update_operation uop;
      uop.issuer = creator_id;
      uop.asset_to_update = sub_asset_1_id;
      uop.new_options = sub_asset_1_id(db).options;
      uop.new_options.flags = sub_asset_1_id(db).options.flags ^ transfer_restricted;
      trx.clear();
      trx.operations.push_back(uop);
      sign(trx, creator_private_key); // Should be signed by the issuer
      PUSH_TX(db, trx);

      // Attempt to return again
      // The attempt should succeed because transfer restrictions have been removed
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, doug_private_key); // Should be signed by the bearer
      PUSH_TX(db, trx);

      // Verify backing
      BOOST_REQUIRE_EQUAL(t1_0.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_0.amount_in_inventory.value, 61); // = 100 - 40 + 1
      BOOST_REQUIRE_EQUAL(t1_0.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_0.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_0.current_backing
                    == asset(expected_required_backing - 500, core_id)); // = (39 NFT subdivision) * (500 core / NFT subdivision)

      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 39); // = 49 - 1
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, core_id), 500); // = (1 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);


      ///
      /// Test whitelisting
      ///
      // Create a whitelist for the token
      BOOST_TEST_MESSAGE( "Creating a whitelist authority for Token #1" );
      ACTOR(izzy)
      upgrade_to_lifetime_member( izzy_id ); // <-- Izzy will need to be a LTM to handle whitelisting duties
      uop = asset_update_operation();
      uop.issuer = creator_id;
      uop.asset_to_update = sub_asset_1_id;
      uop.new_options = sub_asset_1_id(db).options;
      uop.new_options.whitelist_authorities.insert(izzy_id);
      trx.clear();
      trx.operations.push_back(uop);
      sign(trx, creator_private_key); // Should be signed by the issuer
      PUSH_TX(db, trx);
      BOOST_CHECK(sub_asset_1_id(db).options.whitelist_authorities.find(izzy_id) != sub_asset_1_id(db).options.whitelist_authorities.end());

      // Doug attempts to return 3 subdivision of the token
      const share_type t1_return2 = 3;
      return_op = nft_return_operation();
      return_op.amount = asset(t1_return2, sub_asset_1_id);
      return_op.bearer = doug_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, doug_private_key); // Should be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "is not whitelisted for asset");

      // Authorize the returning account
      account_whitelist_operation wop = account_whitelist_operation();
      wop.authorizing_account = izzy_id;
      wop.account_to_list = doug_id;
      wop.new_listing = account_whitelist_operation::white_listed;
      trx.clear();
      trx.operations.push_back(wop);
      sign(trx, izzy_private_key);
      PUSH_TX(db, trx);

      // Attempt to return again
      // The attempt should succeed because the sender and recipient are whitelisted
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, doug_private_key); // Should be signed by the bearer
      PUSH_TX(db, trx);

      // Verify backing
      const nft_token_object &t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 64); // = 100 - 40 + 1 + 3
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(t1_1.current_backing == asset(expected_required_backing - 2000, core_id)); // After some redemption
      // Verify balances
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 40 - 1 - 3); // After returning 4 subdivisions
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, core_id), 2000); // (4 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_REQUIRE_EQUAL(get_balance(GRAPHENE_TEMP_ACCOUNT, core_id), 0);

   } FC_LOG_AND_RETHROW()
}


/**
 * Test the rejection of returns prior to the hardfork
 */
BOOST_AUTO_TEST_CASE(nft_returns_before_hardfork) {
   try {
      // Initialize
      ACTORS((creator)(mgr)(treasury)(doug));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));

      // Advance to before the hardfork time
      generate_blocks(HARDFORK_NFT_M2_TIME - 100);

      // Creator creates an asset
      const string series_name = "SERIESA";
      create_user_issued_asset(series_name, creator_id(db), 0);

      // Creator creates a sub-asset
      const string sub_asset_1_name = "SERIESA.SUB1";
      const asset_object &sub_asset_1 = create_user_issued_asset(sub_asset_1_name, creator_id(db), 0, 1000000, 2);
      const asset_id_type sub_asset_1_id = sub_asset_1.id;

      // Reject returns before the hardfork
      nft_return_operation return_op;
      return_op.amount = asset(5, sub_asset_1_id);
      return_op.bearer = doug_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, doug_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "not yet enabled");

      // Reject returns in a proposal before the hardfork
      {
         proposal_create_operation pop;
         pop.review_period_seconds = 86400;
         uint32_t buffer_seconds = 60 * 60;
         pop.expiration_time = db.head_block_time() + *pop.review_period_seconds + buffer_seconds;
         pop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         pop.proposed_ops.emplace_back(return_op);

         trx.clear();
         trx.operations.push_back(pop);
         // sign(trx, creator_private_key); // No signature required for operations paid by GRAPHENE_TEMP_ACCOUNT
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Not allowed until");
      }
   } FC_LOG_AND_RETHROW()
}

/**
 * Test the burning of tokens that are not NFTs
 */
BOOST_AUTO_TEST_CASE(nft_burn_non_nft) {
   try {
      // Initialize
      advance_past_m2_hardfork();

      ACTORS((creator)(mgr)(beneficiary)(treasury)(doug));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));
      const asset_id_type core_id = asset_id_type();

      // Attempt to burn the core Token
      // which should fail because it is not an NFT
      graphene::chain::nft_burn_operation burn_op;
      burn_op.issuer = creator_id;
      burn_op.amount = asset(1, core_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, creator_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "The token to burn may not be the core token");

      // Create a Series where creator is the Issuer and mgr is the Series Manager
      const string series_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 0;
      create_asset_and_series(series_name, creator_id, creator_private_key,
                              beneficiary_id, mgr_id, royalty_fee_centipercent);

      // Create an asset and mint it into the Series
      const string sub_asset_name = "SERIESA.WILD";
      // Create the sub-asset
      const uint8_t sub_asset_precision = 2;
      const uint64_t& whole_token_subdivisions = asset::scaled_precision(sub_asset_precision).value;
      const uint64_t& max_supply = whole_token_subdivisions;
      const uint16_t flags = DEFAULT_UIA_ASSET_ISSUER_PERMISSION;
      const asset_object &sub_asset_obj = create_user_issued_asset(sub_asset_name, creator_id(db), flags,
                                                                   max_supply, sub_asset_precision);
      const asset_id_type sub_asset_id = sub_asset_obj.id;

      // Attempt to burn the sub-asset
      // which should fail because it is not an NFT
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = creator_id;
      burn_op.amount = asset(1, sub_asset_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, creator_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Burns may only be performed for NFT tokens");

   } FC_LOG_AND_RETHROW()
}

/**
 * Test the burning of NFT tokens that have been minted yet never left the inventory
 * - Token #1 is un-backed
 * - Token #2 is backed
 */
BOOST_AUTO_TEST_CASE(nft_burn_newly_minted) {
   try {
      INVOKE(nft_mint_a);
      advance_past_m2_hardfork();

      // Initialize
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type core_id = asset_id_type();

      ///
      /// Return Token #1 of which consists of 100 subdivision
      /// all of which are in the Inventory
      ///
      // Verify the implementation object
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_1_obj = *token_itr;
      const asset_id_type token_1_id = token_1_obj.token_id;
      BOOST_REQUIRE(token_1_obj.min_price_per_subdivision == asset(0, core_id));
      BOOST_REQUIRE(token_1_obj.req_backing_per_subdivision == asset(0, core_id));
      BOOST_REQUIRE(token_1_obj.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_in_inventory.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_1_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_1_obj.current_backing.asset_id == core_id);

      // Attempt to burn -30% of Token #1
      // which should fail because burn amounts should be positive
      graphene::chain::nft_burn_operation burn_op;
      burn_op.issuer = alice_id;
      burn_op.amount = asset(-30, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a burn should be positive");

      // Attempt to burn 0% of Token #1
      // which should fail because burn amounts should be positive
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(0, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a burn should be positive");

      // Attempt to burn 100.1% of Token #1
      // which should fail because it exceeds the amount available
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(101, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance in the Inventory");

      // Burn 25% of Token #1
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(25, token_1_id); // 25% of 100 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_in_inventory.value, 100 - 25);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_burned.value, 25);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_1_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_1_obj.current_backing.asset_id == core_id);

      // Attempt to burn more than remains of Token #1
      // which should fail
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(76, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance in the Inventory");

      // Burn the remainder of Token #1
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(75, token_1_id); // 75% of 100 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_in_inventory.value, 100 - 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_burned.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_1_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_1_obj.current_backing.asset_id == core_id);

      // Attempt to burn more than remains of Token #1
      // which should fail
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(1, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "A burn is impossible because the Inventory is empty");


      ///
      /// Return Token #2 of which consists of 1000 subdivision
      /// all of which are in the Inventory
      ///
      // Verify the implementation object
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_2_obj = *token_itr;
      const asset_id_type token_2_id = token_2_obj.token_id;
      BOOST_REQUIRE(token_2_obj.min_price_per_subdivision == asset(750, core_id));
      BOOST_REQUIRE(token_2_obj.req_backing_per_subdivision == asset(500, core_id));
      BOOST_REQUIRE(token_2_obj.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_TEST_MESSAGE("Verifying the presence of tokens in the Series Inventory");
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_in_inventory.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_2_obj.current_backing.asset_id == core_id);

      // Attempt to burn -30% of Token #2
      // which should fail because burn amounts should be positive
      burn_op.issuer = alice_id;
      burn_op.amount = asset(-300, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a burn should be positive");

      // Attempt to burn 0% of Token #2
      // which should fail because burn amounts should be positive
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(0, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a burn should be positive");

      // Attempt to burn 100.1% of Token #2
      // which should fail because it exceeds the amount available
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(1001, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance in the Inventory");

      // Burn 25% of Token #1
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(250, token_2_id); // 25% of 1000 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_in_inventory.value, 1000 - 250);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_burned.value, 250);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_2_obj.current_backing.asset_id == core_id);

      // Attempt to burn more than remains of Token #2
      // which should fail
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(751, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance in the Inventory");

      // Burn the remainder of Token #2
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(750, token_2_id); // 75% of 100 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_in_inventory.value, 1000 - 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_burned.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_2_obj.current_backing.asset_id == core_id);

      // Attempt to burn more than remains of Token #2
      // which should fail
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(1, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "A burn is impossible because the Inventory is empty");

   } FC_LOG_AND_RETHROW()
}

/**
 * Test the burning of NFT tokens that have been out in circulation and returned to the Inventory
 * - Token #1 is un-backed
 * - Token #2 is backed
 *
 * Intersperse attempts to mint again after some burns.
 * They should fail regardless of the burns because the maximum amount of the NFT
 * has previously been minted.
 */
BOOST_AUTO_TEST_CASE(nft_burn_of_returned_tokens) {
   try {
      INVOKE(nft_return_of_partial_returns_a);
      advance_past_m2_hardfork();

      // Initialize
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type core_id = asset_id_type();

      ///
      /// Return Token #1 of which consists of 100 subdivision
      /// all of which are in the Inventory, and are not backable
      ///
      // Verify the implementation object
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_1_obj = *token_itr;
      const asset_id_type token_1_id = token_1_obj.token_id;

      // Verify the Inventory's balance after the return
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_in_inventory.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE(token_1_obj.current_backing == asset(0, core_id));
      // Verify everyone's balances after the return
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, token_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, token_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, token_1_id), 0);

      // Attempt to burn -30% of Token #1
      // which should fail because burn amounts should be positive
      graphene::chain::nft_burn_operation burn_op;
      burn_op.issuer = alice_id;
      burn_op.amount = asset(-30, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a burn should be positive");

      // Attempt to burn 0% of Token #1
      // which should fail because burn amounts should be positive
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(0, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a burn should be positive");

      // Attempt to burn 100.1% of Token #1
      // which should fail because it exceeds the amount available
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(101, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance in the Inventory");

      // Bob attempts to Burn 25% of Token #1
      // which should fail because Alice is the issuer
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(25, token_1_id); // 25% of 100 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, bob_private_key); // Should be signed by the Series Issuer but is being signed by another account
      GRAPHENE_REQUIRE_THROW(PUSH_TX(db, trx), fc::exception);

      // Burn 25% of Token #1
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(25, token_1_id); // 25% of 100 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_in_inventory.value, 100 - 25);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_burned.value, 25);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_1_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_1_obj.current_backing.asset_id == core_id);

      // Attempt to mint
      // Minting should fail because the maximum amount of the NFT has already been minted
      BOOST_TEST_MESSAGE("Attempting to mint more of Token #1");
      graphene::chain::nft_mint_operation mint_op;
      mint_op.issuer = alice_id;
      mint_op.asset_id = token_1_id;
      mint_op.subdivisions = 1;
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "would violate the remaining capacity to exist");

      // Attempt to burn more than remains of Token #1
      // which should fail
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(76, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance in the Inventory");

      // Burn the remainder of Token #1
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(75, token_1_id); // 75% of 100 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_in_inventory.value, 100 - 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_burned.value, 100);
      BOOST_REQUIRE_EQUAL(token_1_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_1_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_1_obj.current_backing.asset_id == core_id);

      // Attempt to burn more than remains of Token #1
      // which should fail
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(1, token_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "A burn is impossible because the Inventory is empty");

      // Attempt to mint
      // Minting should fail because the maximum amount of the NFT has already been minted
      BOOST_TEST_MESSAGE("Attempting to mint more of Token #1");
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = token_1_id;
      mint_op.subdivisions = 1;
      mint_op.min_price_per_subdivision = asset(0, core_id); // No minimum price required
      mint_op.req_backing_per_subdivision = asset(0, core_id); // No backing required
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "would violate the remaining capacity to exist");


      ///
      /// Return Token #2 of which consists of 1000 subdivision
      /// all of which are in the Inventory
      ///
      // Verify the implementation object
      token_itr = token_name_idx.find(sub_asset_2_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object &token_2_obj = *token_itr;
      const asset_id_type token_2_id = token_2_obj.token_id;
      BOOST_REQUIRE(token_2_obj.min_price_per_subdivision == asset(750, core_id));
      BOOST_REQUIRE(token_2_obj.req_backing_per_subdivision == asset(500, core_id));
      BOOST_REQUIRE(token_2_obj.current_backing == asset(0, core_id));
      // Verify balances
      BOOST_TEST_MESSAGE("Verifying the presence of tokens in the Series Inventory");
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_in_inventory.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_2_obj.current_backing.asset_id == core_id);

      // Attempt to burn -30% of Token #2
      // which should fail because burn amounts should be positive
      burn_op.issuer = alice_id;
      burn_op.amount = asset(-300, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a burn should be positive");

      // Attempt to burn 0% of Token #2
      // which should fail because burn amounts should be positive
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(0, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Amount of a burn should be positive");

      // Attempt to burn 100.1% of Token #2
      // which should fail because it exceeds the amount available
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(1001, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance in the Inventory");

      // Burn 25% of Token #1
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(250, token_2_id); // 25% of 1000 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_in_inventory.value, 1000 - 250);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_burned.value, 250);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_2_obj.current_backing.asset_id == core_id);

      // Attempt to mint
      // Minting should fail because the maximum amount of the NFT has already been minted
      BOOST_TEST_MESSAGE("Attempting to mint more of Token #2");
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = token_2_id;
      mint_op.subdivisions = 1;
      mint_op.min_price_per_subdivision = asset(750, core_id); // Matches existing definition
      mint_op.req_backing_per_subdivision = asset(500, core_id); // Matches existing definition
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "would violate the remaining capacity to exist");

      // Attempt to burn more than remains of Token #2
      // which should fail
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(751, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "exceeds the available balance in the Inventory");

      // Burn the remainder of Token #2
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(750, token_2_id); // 75% of 100 subdivisions
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);
      // Verify balances
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_minted.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_in_inventory.value, 1000 - 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_burned.value, 1000);
      BOOST_REQUIRE_EQUAL(token_2_obj.amount_on_primary_sale.value, 0);
      BOOST_REQUIRE_EQUAL(token_2_obj.current_backing.amount.value, 0);
      BOOST_REQUIRE(token_2_obj.current_backing.asset_id == core_id);

      // Attempt to burn more than remains of Token #2
      // which should fail
      burn_op = graphene::chain::nft_burn_operation();
      burn_op.issuer = alice_id;
      burn_op.amount = asset(1, token_2_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, alice_private_key); // Should be signed by the Series Issuer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "A burn is impossible because the Inventory is empty");

      // Attempt to mint
      // Minting should fail because the maximum amount of the NFT has already been minted
      BOOST_TEST_MESSAGE("Attempting to mint more of Token #2");
      mint_op = graphene::chain::nft_mint_operation();
      mint_op.issuer = alice_id;
      mint_op.asset_id = token_2_id;
      mint_op.subdivisions = 1;
      mint_op.min_price_per_subdivision = asset(750, core_id); // Matches existing definition
      mint_op.req_backing_per_subdivision = asset(500, core_id); // Matches existing definition
      trx.clear();
      trx.operations.push_back(mint_op);
      sign(trx, alice_private_key);
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "would violate the remaining capacity to exist");

   } FC_LOG_AND_RETHROW()
}


/**
 * Test the multiple primary transfers and burns of a backable NFT token consisting of 100 subdivisions
 * while the token is present both in the Inventory, out in circulation, and burnt.
 */
BOOST_AUTO_TEST_CASE(nft_burn_of_partially_returned_tokens) {
   try {
      advance_past_m2_hardfork();

      ACTORS((creator)(mgr)(beneficiary)(treasury)(doug)(emma));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));
      const asset_id_type core_id = asset_id_type();

      // Create a Series where creator is the Issuer and mgr is the Series Manager
      const string series_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 0;
      create_asset_and_series(series_name, creator_id, creator_private_key,
                              beneficiary_id, mgr_id, royalty_fee_centipercent);

      // Create an asset and mint it into the Series
      // A precision of 2 will result in 10^2 subdivisions = 100 subdivisions
      const string sub_asset_1_name = "SERIESA.SUB1";
      asset req_backing_per_subdivision = asset(500, core_id);
      asset min_price_per_subdivision = asset(750, core_id);
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, 2,
                                                                     creator_id, creator_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision);

      ///
      /// Primary transfer
      /// Manager attempts to primary transfer 70 subdivisions (70%) of the token from the Inventory
      ///
      graphene::chain::nft_primary_transfer_operation ptx_op;

      const share_type treasury_balance_0 = get_balance(treasury_id, core_id);

      BOOST_TEST_MESSAGE("Primary transfer of 70% of Token #1");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(70, sub_asset_1_id);
      ptx_op.to = doug_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      PUSH_TX(db, trx);

      // Verify Inventory
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      auto token_itr = token_name_idx.find(sub_asset_1_name);
      BOOST_REQUIRE(token_itr != token_name_idx.end());
      const nft_token_object& t1_1 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 30); // = 100 - 70
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      share_type expected_required_backing = 35000; // (70 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_REQUIRE(t1_1.current_backing == asset(expected_required_backing, core_id));

      // Verify payment by funding source
      // Interpretation #1 of the concept design requires enough payment
      // to cover the backing behind the amount of token extracted from the Inventory
      const share_type expected_treasury_balance_1 = treasury_balance_0 - expected_required_backing;
      const share_type treasury_balance_1 = get_balance(treasury_id, core_id);
      BOOST_CHECK(expected_treasury_balance_1 == treasury_balance_1);

      // Verify account balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 70);

      ///
      /// Perform a partial return of 15 subdivisions
      ///
      const share_type t1_return1 = 15;
      nft_return_operation return_op;
      return_op.amount = asset(t1_return1, sub_asset_1_id);
      return_op.bearer = doug_id;
      trx.clear();
      trx.operations.push_back(return_op);
      sign(trx, doug_private_key); // Must be signed by the bearer
      PUSH_TX(db, trx);

      // Verify Inventory
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 45); // = 100 - 70 + 15
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 0);
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      expected_required_backing = 27500; // (55 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_REQUIRE(t1_1.current_backing == asset(expected_required_backing, core_id));

      // Verify account balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 55); // 70 - 15
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, core_id), 7500); // (15 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_CHECK(get_balance(treasury_id, core_id) == treasury_balance_1); // Treasury's balance should be unaffected


      ///
      /// Burn some of the subdivisions that are present in the Inventory.
      /// The only quantities that should be affected are after the burning are
      /// the amount burned, and the amount in Inventory.
      ///
      graphene::chain::nft_burn_operation burn_op;
      burn_op.issuer = creator_id;
      burn_op.amount = asset(25, sub_asset_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, creator_private_key); // Should be signed by the Series Issuer
      PUSH_TX(db, trx);

      // Verify Inventory
      BOOST_REQUIRE_EQUAL(t1_1.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_1.amount_in_inventory.value, 20); // = 100 - 70 + 15 - 25
      BOOST_REQUIRE_EQUAL(t1_1.amount_burned.value, 25); // = 0 + 25
      BOOST_REQUIRE_EQUAL(t1_1.amount_on_primary_sale.value, 0);
      expected_required_backing = 27500; // (70 - 15 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_REQUIRE(t1_1.current_backing == asset(expected_required_backing, core_id));

      // Verify account balances after the burn
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 55); // 70 - 15
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, core_id), 7500); // (15 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_CHECK(get_balance(treasury_id, core_id) == treasury_balance_1); // Treasury's balance should be unaffected

      ///
      //// Primary transfer #2
      //// Manager attempts to primary transfer 5 subdivisions (5%) of the token from the Inventory
      ///
      BOOST_TEST_MESSAGE("Primary transfer of 5% of Token #1");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(5, sub_asset_1_id);
      ptx_op.to = emma_id;
      ptx_op.manager = mgr_id;
      ptx_op.provisioner = treasury_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgr_private_key);
      sign(trx, treasury_private_key);
      PUSH_TX(db, trx);

      // Verify Inventory
      const nft_token_object& t1_2 = *token_itr;
      BOOST_REQUIRE_EQUAL(t1_2.amount_minted.value, 100);
      BOOST_REQUIRE_EQUAL(t1_2.amount_in_inventory.value, 15); // = 100 - 70 + 15 - 25 - 5
      BOOST_REQUIRE_EQUAL(t1_2.amount_burned.value, 25); // = 0 + 25
      BOOST_REQUIRE_EQUAL(t1_2.amount_on_primary_sale.value, 0);
      expected_required_backing = 30000; // (70 - 15 + 5 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_REQUIRE(t1_2.current_backing == asset(expected_required_backing, core_id));

      // Verify payment by funding source
      // Interpretation #1 of the concept design requires enough payment
      // to cover the backing behind the amount of token extracted from the Inventory
      // Treasury funded (70 + 5 NFT subdivision) * (500 core / NFT subdivision)
      const share_type expected_treasury_balance_2 = treasury_balance_0 - (37500);
      const share_type treasury_balance_2 = get_balance(treasury_id, core_id);
      BOOST_CHECK(expected_treasury_balance_2 == treasury_balance_2);

      // Verify account balances after the primary transfer
      BOOST_REQUIRE_EQUAL(get_balance(creator_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(beneficiary_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(mgr_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 55); // = 70 - 15
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, core_id), 7500); // (15 NFT subdivision) * (500 core / NFT subdivision)
      BOOST_CHECK(get_balance(treasury_id, core_id) == treasury_balance_2); // Treasury's balance should be unaffected
      BOOST_REQUIRE_EQUAL(get_balance(emma_id, sub_asset_1_id), 5); // 5
      BOOST_REQUIRE_EQUAL(get_balance(emma_id, core_id), 0);

   } FC_LOG_AND_RETHROW()
}

/**
 * Test the rejection of burns prior to the hardfork
 */
BOOST_AUTO_TEST_CASE(nft_burn_before_hardfork) {
   try {
      // Initialize
      ACTORS((creator)(mgr)(treasury)(doug));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasury_id, graphene::chain::asset(init_balance));

      // Advance to before the hardfork time
      generate_blocks(HARDFORK_NFT_M2_TIME - 100);

      // Creator creates an asset
      const string series_name = "SERIESA";
      create_user_issued_asset(series_name, creator_id(db), 0);

      // Creator creates a sub-asset
      const string sub_asset_1_name = "SERIESA.SUB1";
      const asset_object &sub_asset_1 = create_user_issued_asset(sub_asset_1_name, creator_id(db), 0, 1000000, 2);
      const asset_id_type sub_asset_1_id = sub_asset_1.id;

      // Reject burns before the hardfork
      nft_burn_operation burn_op;
      burn_op.issuer = creator_id;
      burn_op.amount = asset(5, sub_asset_1_id);
      trx.clear();
      trx.operations.push_back(burn_op);
      sign(trx, creator_private_key); // Must be signed by the bearer
      REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "not yet enabled");

      // Reject burns in a proposal before the hardfork
      {
         proposal_create_operation pop;
         pop.review_period_seconds = 86400;
         uint32_t buffer_seconds = 60 * 60;
         pop.expiration_time = db.head_block_time() + *pop.review_period_seconds + buffer_seconds;
         pop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
         pop.proposed_ops.emplace_back(burn_op);

         trx.clear();
         trx.operations.push_back(pop);
         // sign(trx, creator_private_key); // No signature required for operations paid by GRAPHENE_TEMP_ACCOUNT
         REQUIRE_EXCEPTION_WITH_TEXT(PUSH_TX(db, trx), "Not allowed until");
      }
   } FC_LOG_AND_RETHROW()
}


/**
 * Tests of Database API functionality
 */

/**
 * Convert an asset ID to a conventional string representation (e.g. "1.3.0")
 * @param id Asset ID
 * @return String representation
 */
std::string asset_id_to_string(asset_id_type id) {
   std::string asset_id = fc::to_string(id.space_id) +
                          "." + fc::to_string(id.type_id) +
                          "." + fc::to_string(id.instance.value);
   return asset_id;
}

/**
 * Test the database API ability to query a series by its associated asset
 */
BOOST_AUTO_TEST_CASE( db_api_get_series_by_asset ) {
   try {
      advance_past_m1_hardfork();

      // Initialize
      ACTORS((creator)(mgr)(beneficiary));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creator_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgr_id, graphene::chain::asset(init_balance));

      // Create a Series where creator is the Issuer and mgr is the Series Manager
      const string series_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 5 * GRAPHENE_1_PERCENT;
      const asset_id_type series_asset_id = create_asset_and_series(series_name, creator_id, creator_private_key,
                              beneficiary_id, mgr_id, royalty_fee_centipercent);

      // Query the database API
      graphene::app::database_api db_api(db, &(this->app.get_options()));

      // Query by asset name
      {
         BOOST_TEST_MESSAGE("Querying the database API by Series/Asset name");
         optional<nft_series_object> opt_series_obj = db_api.get_series_by_asset(series_name);
         BOOST_REQUIRE(opt_series_obj.valid());
         nft_series_object series_obj = *db_api.get_series_by_asset(series_name);
         BOOST_CHECK(series_obj.beneficiary == beneficiary_id);
         BOOST_CHECK(series_obj.manager == mgr_id);
         BOOST_CHECK(series_obj.asset_id == series_asset_id);
         BOOST_CHECK_EQUAL(series_obj.royalty_fee_centipercent, 500);
         BOOST_CHECK_EQUAL(series_obj.series_name, series_name);
      }

      // Query by asset ID
      {
         BOOST_TEST_MESSAGE("Querying the database API by Series associated asset ID: " + asset_id_to_string(series_asset_id));
         nft_series_object series_obj = *db_api.get_series_by_asset(asset_id_to_string(series_asset_id));
         BOOST_CHECK(series_obj.beneficiary == beneficiary_id);
         BOOST_CHECK(series_obj.manager == mgr_id);
         BOOST_CHECK(series_obj.asset_id == series_asset_id);
         BOOST_CHECK_EQUAL(series_obj.royalty_fee_centipercent, 500);
         BOOST_CHECK_EQUAL(series_obj.series_name, series_name);
      }

      // Query for a non-existent asset
      BOOST_TEST_MESSAGE("Querying the database API for non existing asset");
      BOOST_CHECK(!db_api.get_series_by_asset("non-existent-asset").valid());

   } FC_LOG_AND_RETHROW()
}

/**
 * Test the database API ability to query all series
 */
BOOST_AUTO_TEST_CASE( db_api_list_series ) {
   try {
      advance_past_m1_hardfork();

      // Initialize
      ACTORS((creatora)(creatorb)(mgra)(mgrb)(beneficiarya)(beneficiaryb));
      int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creatora_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgra_id, graphene::chain::asset(init_balance));

      // Query the database API
      graphene::app::database_api db_api(db, &(this->app.get_options()));
      vector<nft_series_object> list;
      graphene::chain::nft_series_id_type non_existing_series_id(999);

      ///
      /// No Series exist
      ///
      // List series when no series exist
      {
         BOOST_TEST_MESSAGE("Querying all series: expecting no series");
         list = db_api.list_series(); // Without any filter
         BOOST_CHECK_EQUAL(list.size(), 0);

         list = db_api.list_series(3); // Without any lower bound
         BOOST_CHECK_EQUAL(list.size(), 0);

         list = db_api.list_series(10, non_existing_series_id);  // With a non-existent lower bound
         BOOST_CHECK_EQUAL(list.size(), 0);
      }


      ///
      /// Create a Series to reach a total of 1 Series
      ///
      const string series_a1_name = "SERIESA1";
      uint16_t royalty_fee_centipercent = 5 * GRAPHENE_1_PERCENT;
      const asset_id_type series_a1_asset_id = create_asset_and_series(series_a1_name, creatora_id,
                                                                       creatora_private_key,
                                                                       beneficiarya_id, mgra_id,
                                                                       royalty_fee_centipercent);


      // List series when 1 series exists
      {
         BOOST_TEST_MESSAGE("Querying all series: expecting 1 series");
         list = db_api.list_series(); // Without any filter
         BOOST_CHECK_EQUAL(list.size(), 1);

         list = db_api.list_series(3); // Without any lower bound
         BOOST_CHECK_EQUAL(list.size(), 1);

         list = db_api.list_series(3, non_existing_series_id);  // With a non-existent lower bound
         BOOST_CHECK_EQUAL(list.size(), 0);
      }

      ///
      /// Create a Series to reach a total of 5 Series
      ///
      const string series_b1_name = "SERIESB1";
      const asset_id_type series_b1_asset_id = create_asset_and_series(series_b1_name, creatorb_id,
                                                                       creatorb_private_key,
                                                                       beneficiarya_id, mgra_id,
                                                                       royalty_fee_centipercent);

      const string series_a2_name = "SERIESA2";
      const asset_id_type series_a2_asset_id = create_asset_and_series(series_a2_name, creatora_id,
                                                                       creatora_private_key,
                                                                       beneficiarya_id, mgra_id,
                                                                       royalty_fee_centipercent);

      const string series_a3_name = "SERIESA3";
      const asset_id_type series_a3_asset_id = create_asset_and_series(series_a3_name, creatora_id,
                                                                       creatora_private_key,
                                                                       beneficiarya_id, mgra_id,
                                                                       royalty_fee_centipercent);

      const string series_b2_name = "SERIESB2";
      const asset_id_type series_b2_asset_id = create_asset_and_series(series_b2_name, creatorb_id,
                                                                       creatorb_private_key,
                                                                       beneficiarya_id, mgra_id,
                                                                       royalty_fee_centipercent);

      // List series when 5 exist
      {
         BOOST_TEST_MESSAGE("Querying all series: expecting 5 series");
         list = db_api.list_series(); // Without any filter
         BOOST_CHECK_EQUAL(list.size(), 5);

         list = db_api.list_series(3); // Without any lower bound
         BOOST_CHECK_EQUAL(list.size(), 3); // <-- Should be limited to the first 3
         BOOST_CHECK_EQUAL(list[0].id.instance(), 0);
         BOOST_CHECK_EQUAL(list[1].id.instance(), 1);
         BOOST_CHECK_EQUAL(list[2].id.instance(), 2);

         list = db_api.list_series(3, graphene::chain::nft_series_id_type(3)); // With a lower bound
         BOOST_CHECK_EQUAL(list.size(), 2); // <-- Should be limited to the remaining 2
         BOOST_CHECK_EQUAL(list[0].id.instance(), 3);
         BOOST_CHECK_EQUAL(list[1].id.instance(), 4);

         list = db_api.list_series(3, non_existing_series_id);  // With a non-existent lower bound
         BOOST_CHECK_EQUAL(list.size(), 0);

         // Query each available series one at a time
         list = db_api.list_series(1, graphene::chain::nft_series_id_type(0));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK_EQUAL(list[0].id.instance(), 0);
         BOOST_CHECK(list[0].asset_id == series_a1_asset_id);

         list = db_api.list_series(1, graphene::chain::nft_series_id_type(1));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK_EQUAL(list[0].id.instance(), 1);
         BOOST_CHECK(list[0].asset_id == series_b1_asset_id);

         list = db_api.list_series(1, graphene::chain::nft_series_id_type(2));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK_EQUAL(list[0].id.instance(), 2);
         BOOST_CHECK(list[0].asset_id == series_a2_asset_id);

         list = db_api.list_series(1, graphene::chain::nft_series_id_type(3));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK_EQUAL(list[0].id.instance(), 3);
         BOOST_CHECK(list[0].asset_id == series_a3_asset_id);

         list = db_api.list_series(1, graphene::chain::nft_series_id_type(4));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK_EQUAL(list[0].id.instance(), 4);
         BOOST_CHECK(list[0].asset_id == series_b2_asset_id);

         list = db_api.list_series(1, graphene::chain::nft_series_id_type(5));
         BOOST_CHECK_EQUAL(list.size(), 0);
      }

   } FC_LOG_AND_RETHROW()
}

/**
 * Test the database API ability to query a token by its associated asset
 */
BOOST_AUTO_TEST_CASE( db_api_tokens ) {
   try {
      // Initialize
      INVOKE(db_api_list_series);
      GET_ACTOR(creatora);
      GET_ACTOR(creatorb);
      // Series name Should match the series name from the referenced db_api_list_series
      const string series_a_name = "SERIESA1";
      const string series_b_name = "SERIESB1";
      const asset_id_type core_id = asset_id_type();
      graphene::app::database_api db_api(db, &(this->app.get_options()));
      vector <nft_token_object> list;
      graphene::chain::nft_token_id_type non_existing_token_id(999);

      ///
      /// Query for Series tokens when none yet exist
      ///
      BOOST_TEST_MESSAGE("Querying all tokens: expecting none");
      // List tokens when none exist
      {
         BOOST_TEST_MESSAGE("Querying all tokens: expecting none in series");
         list = db_api.list_tokens_by_series_name(series_a_name); // Without any filter
         BOOST_CHECK_EQUAL(list.size(), 0);

         list = db_api.list_tokens_by_series_name(series_a_name, 3); // Without any lower bound
         BOOST_CHECK_EQUAL(list.size(), 0);

         list = db_api.list_tokens_by_series_name(series_a_name, 10,
                                                  non_existing_token_id);  // With a non-existent lower bound
         BOOST_CHECK_EQUAL(list.size(), 0);
      }

      ///
      /// Create 5 tokens in Series A, and 3 tokens in Series B
      /// but intermingle their creations to exercise the index "by_nft_token_series_id"
      /// The creation sequence shall be:
      /// 1. A1.SUB1
      /// 2. B1.SUB1
      /// 3. A1.SUB2
      /// 4. A1.SUB3
      /// 5. B1.SUB2
      /// 6. B1.SUB3
      /// 7. A1.SUB4
      /// 8. A1.SUB5
      ///
      asset min_price_per_subdivision;
      asset req_backing_per_subdivision;
      uint8_t token_precision;

      // Create Token A1.SUB1
      const string token_A1_name = series_a_name + ".SUB1";
      min_price_per_subdivision = asset(100, core_id);
      req_backing_per_subdivision = asset(0, core_id);
      token_precision = 1; // An entire single token should have (10^precision = 10^1 = 10) subdivisions
      const asset_id_type token_A1_id = create_sub_asset_and_mint(token_A1_name, token_precision,
                                                                 creatora_id, creatora_private_key,
                                                                 req_backing_per_subdivision,
                                                                 min_price_per_subdivision);

      // Create Token B1.SUB1
      const string token_B1_name = series_b_name + ".SUB1";
      min_price_per_subdivision = asset(100, core_id);
      req_backing_per_subdivision = asset(50, core_id);
      token_precision = 1; // An entire single token should have (10^precision = 10^1 = 10) subdivisions
      const asset_id_type token_B1_id = create_sub_asset_and_mint(token_B1_name, token_precision,
                                                                 creatorb_id, creatorb_private_key,
                                                                 req_backing_per_subdivision,
                                                                 min_price_per_subdivision);

      // Create Token A1.SUB2
      const string token_A2_name = series_a_name + ".SUB2";
      min_price_per_subdivision = asset(200, core_id);
      req_backing_per_subdivision = asset(0, core_id);
      token_precision = 2; // An entire single token should have (10^precision = 10^2 = 100) subdivisions
      const asset_id_type token_A2_id = create_sub_asset_and_mint(token_A2_name, token_precision,
                                                                 creatora_id, creatora_private_key,
                                                                 req_backing_per_subdivision,
                                                                 min_price_per_subdivision);

      // Create Token A1.SUB3
      const string token_A3_name = series_a_name + ".SUB3";
      min_price_per_subdivision = asset(300, core_id);
      req_backing_per_subdivision = asset(0, core_id);
      token_precision = 3; // An entire single token should have (10^precision = 10^3 = 1000) subdivisions
      const asset_id_type token_A3_id = create_sub_asset_and_mint(token_A3_name, token_precision,
                                                                 creatora_id, creatora_private_key,
                                                                 req_backing_per_subdivision,
                                                                 min_price_per_subdivision);

      // Create Token B1.SUB2
      const string token_B2_name = series_b_name + ".SUB2";
      min_price_per_subdivision = asset(200, core_id);
      req_backing_per_subdivision = asset(150, core_id);
      token_precision = 2; // An entire single token should have (10^precision = 10^2 = 100) subdivisions
      const asset_id_type token_B2_id = create_sub_asset_and_mint(token_B2_name, token_precision,
                                                                 creatorb_id, creatorb_private_key,
                                                                 req_backing_per_subdivision,
                                                                 min_price_per_subdivision);

      // Create Token B1.SUB3
      const string token_B3_name = series_b_name + ".SUB3";
      min_price_per_subdivision = asset(300, core_id);
      req_backing_per_subdivision = asset(250, core_id);
      token_precision = 3; // An entire single token should have (10^precision = 10^3 = 1000) subdivisions
      const asset_id_type token_B3_id = create_sub_asset_and_mint(token_B3_name, token_precision,
                                                                 creatorb_id, creatorb_private_key,
                                                                 req_backing_per_subdivision,
                                                                 min_price_per_subdivision);

      // Create Token A1.SUB4
      const string token_A4_name = series_a_name + ".SUB4";
      min_price_per_subdivision = asset(400, core_id);
      req_backing_per_subdivision = asset(0, core_id);
      token_precision = 4; // An entire single token should have (10^precision = 10^4 = 10000) subdivisions
      const asset_id_type token_A4_id = create_sub_asset_and_mint(token_A4_name, token_precision,
                                                                  creatora_id, creatora_private_key,
                                                                  req_backing_per_subdivision,
                                                                  min_price_per_subdivision);

      // Create Token A1.SUB5
      const string token_A5_name = series_a_name + ".SUB5";
      min_price_per_subdivision = asset(500, core_id);
      req_backing_per_subdivision = asset(0, core_id);
      token_precision = 5; // An entire single token should have (10^precision = 10^5 = 100000) subdivisions
      const asset_id_type token_A5_id = create_sub_asset_and_mint(token_A5_name, token_precision,
                                                                  creatora_id, creatora_private_key,
                                                                  req_backing_per_subdivision,
                                                                  min_price_per_subdivision);

      ///
      /// Query by each token's associated asset ID
      ///
      // Query Token A1.SUB1 by asset name
      {
         BOOST_TEST_MESSAGE("Querying the database API by Token/Asset name");
         nft_token_object token_obj = *db_api.get_token_by_asset(token_A1_name);
         BOOST_REQUIRE(token_obj.min_price_per_subdivision == asset(100, core_id));
         BOOST_REQUIRE(token_obj.req_backing_per_subdivision == asset(0, core_id));
         BOOST_REQUIRE(token_obj.current_backing == asset(0, core_id));
         // Verify balances
         BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 10);
         BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 10);
         BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
         BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
         BOOST_CHECK_EQUAL(token_obj.current_backing.amount.value, 0);
         BOOST_CHECK(token_obj.current_backing.asset_id == core_id);
      }

      // Query Token A1.SUB1 by asset ID
      {
         BOOST_TEST_MESSAGE("Querying the database API by Token/Asset ID");
         nft_token_object token_obj = *db_api.get_token_by_asset(asset_id_to_string(token_A1_id));
         BOOST_REQUIRE(token_obj.min_price_per_subdivision == asset(100, core_id));
         BOOST_REQUIRE(token_obj.req_backing_per_subdivision == asset(0, core_id));
         BOOST_REQUIRE(token_obj.current_backing == asset(0, core_id));
         // Verify balances
         BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 10);
         BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 10);
         BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
         BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
         BOOST_CHECK_EQUAL(token_obj.current_backing.amount.value, 0);
         BOOST_CHECK(token_obj.current_backing.asset_id == core_id);
      }

      // Query Token B1.SUB1 by asset name
      {
         BOOST_TEST_MESSAGE("Querying the database API by Token/Asset name");
         nft_token_object token_obj = *db_api.get_token_by_asset(token_B1_name);
         BOOST_REQUIRE(token_obj.min_price_per_subdivision == asset(100, core_id));
         BOOST_REQUIRE(token_obj.req_backing_per_subdivision == asset(50, core_id));
         BOOST_REQUIRE(token_obj.current_backing == asset(0, core_id));
         // Verify balances
         BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 10);
         BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 10);
         BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
         BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
         BOOST_CHECK_EQUAL(token_obj.current_backing.amount.value, 0);
         BOOST_CHECK(token_obj.current_backing.asset_id == core_id);
      }

      // Query Token B1.SUB1 by asset ID
      {
         BOOST_TEST_MESSAGE("Querying the database API by Token/Asset ID");
         nft_token_object token_obj = *db_api.get_token_by_asset(asset_id_to_string(token_B1_id));
         BOOST_REQUIRE(token_obj.min_price_per_subdivision == asset(100, core_id));
         BOOST_REQUIRE(token_obj.req_backing_per_subdivision == asset(50, core_id));
         BOOST_REQUIRE(token_obj.current_backing == asset(0, core_id));
         // Verify balances
         BOOST_CHECK_EQUAL(token_obj.amount_minted.value, 10);
         BOOST_CHECK_EQUAL(token_obj.amount_in_inventory.value, 10);
         BOOST_CHECK_EQUAL(token_obj.amount_burned.value, 0);
         BOOST_CHECK_EQUAL(token_obj.amount_on_primary_sale.value, 0);
         BOOST_CHECK_EQUAL(token_obj.current_backing.amount.value, 0);
         BOOST_CHECK(token_obj.current_backing.asset_id == core_id);
      }

      // Query for a non-existent asset
      BOOST_TEST_MESSAGE("Querying the database API for non existing asset");
      BOOST_CHECK(!db_api.get_token_by_asset("non-existent-asset").valid());

      graphene::chain::asset_id_type non_existing_asset_id(999);
      BOOST_CHECK(!db_api.get_token_by_asset(asset_id_to_string(non_existing_asset_id)).valid());

      ///
      /// Query for tokens in a Series
      ///
      // List tokens from a Series A1
      {
         BOOST_TEST_MESSAGE("Querying all tokens from Series A1: expecting 5");
         list = db_api.list_tokens_by_series_name(series_a_name);
         BOOST_CHECK_EQUAL(list.size(), 5);

         list = db_api.list_tokens_by_series_name(series_a_name, 3); // Without any lower bound
         BOOST_CHECK_EQUAL(list.size(), 3); // <-- Should be limited to the first 3
         BOOST_CHECK(list[0].token_id == token_A1_id);
         BOOST_CHECK(list[1].token_id == token_A2_id);
         BOOST_CHECK(list[2].token_id == token_A3_id);

         list = db_api.list_tokens_by_series_name(series_a_name, 3,
                                                  graphene::chain::nft_token_id_type(list[2].id.instance() + 1)); // With a lower bound
         BOOST_CHECK_EQUAL(list.size(), 2); // <-- Should be limited to the remaining 2
         BOOST_CHECK(list[0].token_id == token_A4_id);
         BOOST_CHECK(list[1].token_id == token_A5_id);

         list = db_api.list_tokens_by_series_name(series_a_name, 3,
                                                  non_existing_token_id);  // With a non-existent lower bound
         BOOST_CHECK_EQUAL(list.size(), 0);

         // Query each available series one at a time
         list = db_api.list_tokens_by_series_name(series_a_name, 1, graphene::chain::nft_token_id_type(0));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK(list[0].token_id == token_A1_id);

         list = db_api.list_tokens_by_series_name(series_a_name, 1, graphene::chain::nft_token_id_type(list[0].id.instance() + 1));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK(list[0].token_id == token_A2_id);

         list = db_api.list_tokens_by_series_name(series_a_name, 1, graphene::chain::nft_token_id_type(list[0].id.instance() + 1));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK(list[0].token_id == token_A3_id);

         list = db_api.list_tokens_by_series_name(series_a_name, 1, graphene::chain::nft_token_id_type(list[0].id.instance() + 1));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK(list[0].token_id == token_A4_id);

         list = db_api.list_tokens_by_series_name(series_a_name, 1, graphene::chain::nft_token_id_type(list[0].id.instance() + 1));
         BOOST_CHECK_EQUAL(list.size(), 1);
         BOOST_CHECK(list[0].token_id == token_A5_id);

         list = db_api.list_tokens_by_series_name(series_a_name, 1, graphene::chain::nft_token_id_type(list[0].id.instance() + 1));
         BOOST_CHECK_EQUAL(list.size(), 0);
      }

      // List tokens from a series containing none
      {
         BOOST_TEST_MESSAGE("Querying all tokens from Series B1: expecting 3");
         list = db_api.list_tokens_by_series_name(series_b_name); // Without any filter
         BOOST_CHECK_EQUAL(list.size(), 3);
         BOOST_CHECK(list[0].token_id == token_B1_id);
         BOOST_CHECK(list[1].token_id == token_B2_id);
         BOOST_CHECK(list[2].token_id == token_B3_id);

         list = db_api.list_tokens_by_series_name(series_b_name, 2); // Without any lower bound
         BOOST_CHECK_EQUAL(list.size(), 2);
         BOOST_CHECK(list[0].token_id == token_B1_id);
         BOOST_CHECK(list[1].token_id == token_B2_id);

         list = db_api.list_tokens_by_series_name(series_b_name, 10,
                                                  non_existing_token_id);  // With a non-existent lower bound
         BOOST_CHECK_EQUAL(list.size(), 0);
      }

   } FC_LOG_AND_RETHROW()
}

/**
 * Test the database API ability to report NFT events in account history
 */
BOOST_AUTO_TEST_CASE( db_api_account_history_a ) {
   try {
      BOOST_TEST_MESSAGE("Verifying NFT activity in account histories");

      // Initialize with another scenario
      INVOKE(nft_return_of_complete_returns_a);
      // Mimic the variables from the invoked test
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(charlie);
      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;

      // To permit the most recent account history to embed itself into a block
      // and thereby become accessible in account history
      generate_block();
      graphene::app::history_api hist_api(app);

      ///
      /// Inspect Charlie's account history
      ///
      vector <operation_history_object> histories;
      operation op;
      histories = hist_api.get_account_history("charlie");
      int count = histories.size();
      // Charlie's history in this scenario should include:
      // 1 Creation of Charlie's account
      // 4 NFT Primary Transfer to Charlie
      // 2 NFT Returns by Charlie
      // 1 NFT Redeemed for Charlie because 1 of the NFT returns was for a backed NFT
      BOOST_CHECK(count == 8);

      // Account histories are sorted in decreasing time order
      // The first operation should correspond to Charlie's virtual redemption operation
      // for the return of 1000 subdivision of Token #2
      op = histories[0].op;
      BOOST_REQUIRE(op.is_type<nft_redeemed_operation>());
      nft_redeemed_operation redeemed_op = op.get<nft_redeemed_operation>();
      BOOST_CHECK(redeemed_op.bearer == charlie_id);
      BOOST_CHECK(redeemed_op.redemption == asset(1000, sub_asset_2_id));
      BOOST_CHECK(redeemed_op.redemption_value == asset(500000, core_id));

      // The next operation should correspond to Charlie's returning 1000 subdivision of Token #2
      op = histories[1].op;
      BOOST_REQUIRE(op.is_type<nft_return_operation>());
      nft_return_operation return_op = op.get<nft_return_operation>();
      BOOST_CHECK(return_op.bearer == charlie_id);
      BOOST_CHECK(return_op.amount == asset(1000, sub_asset_2_id));

      // The next operation should correspond to Charlie's returning 40 subdivision of Token #1
      op = histories[2].op;
      BOOST_REQUIRE(op.is_type<nft_return_operation>());
      return_op = op.get<nft_return_operation>();
      BOOST_CHECK(return_op.bearer == charlie_id);
      BOOST_CHECK(return_op.amount == asset(40, sub_asset_1_id));

      // The next operation should correspond to Charlie's receipt of 250 subdivision of Token #2
      op = histories[3].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      nft_primary_transfer_operation ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == charlie_id);
      BOOST_CHECK(ptx_op.amount == asset(250, sub_asset_2_id));

      // The next operation should correspond to Charlie's receipt of 350 subdivision of Token #2
      op = histories[4].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == charlie_id);
      BOOST_CHECK(ptx_op.amount == asset(350, sub_asset_2_id));

      // The next operation should correspond to Charlie's receipt of 400 subdivision of Token #2
      op = histories[5].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == charlie_id);
      BOOST_CHECK(ptx_op.amount == asset(400, sub_asset_2_id));

      // The next operation should correspond to Charlie's receipt of 40 subdivision of Token #1
      op = histories[6].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == charlie_id);
      BOOST_CHECK(ptx_op.amount == asset(40, sub_asset_1_id));

      // The next operation should correspond to Charlie's account creation
      op = histories[7].op;
      BOOST_REQUIRE(op.is_type<account_create_operation>());
      account_create_operation ac_op = op.get<account_create_operation>();
      BOOST_CHECK_EQUAL(ac_op.name, "charlie");

   } FC_LOG_AND_RETHROW()
}


/**
 * Test the database API ability to report NFT events in account history
 */
BOOST_AUTO_TEST_CASE( db_api_account_history_nft_burn_of_returned_tokens ) {
   try {
      BOOST_TEST_MESSAGE("Verifying NFT activity in account histories: nft_burn_of_returned_tokens");

      // Initialize with another scenario
      INVOKE(nft_burn_of_returned_tokens);
      // Mimic the variables from the invoked test
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(alice);
      GET_ACTOR(charlie);
      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type series_id = get_asset(series_name).id;
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;

      // To permit the most recent account history to embed itself into a block
      // and thereby become accessible in account history
      generate_block();
      graphene::app::history_api hist_api(app);

      ///
      /// Inspect Alice's account history
      ///
      vector <operation_history_object> histories;
      histories = hist_api.get_account_history("alice");
      int count = histories.size();
      // Alices's history in this scenario should include:
      // 1 Account creation
      // 1 Transfer of CORE to Alice
      // 1 UIA Creation
      // 1 Series Creation
      // 1 Create UIA #1
      // 1 NFT Mint Token #1 (Associate UIA #1 with the Series)
      // 1 Create UIA #2
      // 1 NFT Mint Token #2 (Associate UIA #2 with the Series)
      // 4 NFT Primary Transfers
      // 4 NFT Burns
      BOOST_CHECK_EQUAL(count, 16);

      operation op;

      // Account histories are sorted in decreasing time order
      // The first operation should correspond to Alice's burning (as Series Issuer) of 750 subdivision of Token #2
      op = histories[0].op;
      BOOST_REQUIRE(op.is_type<nft_burn_operation>());
      nft_burn_operation burn_op = op.get<nft_burn_operation>();
      BOOST_CHECK(burn_op.issuer == alice_id);
      BOOST_CHECK(burn_op.amount == asset(750, sub_asset_2_id));

      // The next operation should correspond to Alice's burning (as Series Issuer) of 250 subdivision of Token #2
      op = histories[1].op;
      BOOST_REQUIRE(op.is_type<nft_burn_operation>());
      burn_op = op.get<nft_burn_operation>();
      BOOST_CHECK(burn_op.issuer == alice_id);
      BOOST_CHECK(burn_op.amount == asset(250, sub_asset_2_id));

      // The next operation should correspond to Alice's burning (as Series Issuer) of 75 subdivision of Token #1
      op = histories[2].op;
      BOOST_REQUIRE(op.is_type<nft_burn_operation>());
      burn_op = op.get<nft_burn_operation>();
      BOOST_CHECK(burn_op.issuer == alice_id);
      BOOST_CHECK(burn_op.amount == asset(75, sub_asset_1_id));

      // The next operation should correspond to Alice's burning (as Series Issuer) of 25 subdivision of Token #1
      op = histories[3].op;
      BOOST_REQUIRE(op.is_type<nft_burn_operation>());
      burn_op = op.get<nft_burn_operation>();
      BOOST_CHECK(burn_op.issuer == alice_id);
      BOOST_CHECK(burn_op.amount == asset(25, sub_asset_1_id));

      // The next operation should correspond to Alice's primary transfer (as Series Manager) of 250 subdivision of Token #2
      op = histories[4].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      nft_primary_transfer_operation ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == charlie_id);
      BOOST_CHECK(ptx_op.amount == asset(250, sub_asset_2_id));

      // The next operation should correspond to Alice's primary transfer (as Series Manager) of 350 subdivision of Token #2
      op = histories[5].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == charlie_id);
      BOOST_CHECK(ptx_op.amount == asset(350, sub_asset_2_id));

      // The next operation should correspond to Alice's primary transfer (as Series Manager) of 400 subdivision of Token #2
      op = histories[6].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == charlie_id);
      BOOST_CHECK(ptx_op.amount == asset(400, sub_asset_2_id));

      // The next operation should correspond to Alice's primary transfer (as Series Manager) of 40 subdivision of Token #1
      op = histories[7].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == charlie_id);
      BOOST_CHECK(ptx_op.amount == asset(40, sub_asset_1_id));

      // The next operation should correspond to Alice's minting (as Series Issuer) of 1000 subdivision of Token #2
      op = histories[8].op;
      BOOST_REQUIRE(op.is_type<nft_mint_operation>());
      nft_mint_operation mint_op = op.get<nft_mint_operation>();
      BOOST_CHECK(mint_op.issuer == alice_id);
      BOOST_CHECK(mint_op.asset_id == sub_asset_2_id);
      BOOST_CHECK_EQUAL(mint_op.subdivisions.value, 1000);

      // The next operation should correspond to Alice's creation of Token #2 as a UIA sub-asset
      op = histories[9].op;
      BOOST_REQUIRE(op.is_type<asset_create_operation>());
      asset_create_operation asset_create_op = op.get<asset_create_operation  >();
      BOOST_CHECK(asset_create_op.issuer == alice_id);
      BOOST_CHECK(asset_create_op.symbol == sub_asset_2_name);
      BOOST_CHECK(asset_create_op.precision == 3); // 10^3 = 1000 subdivision for a single unit

      // The next operation should correspond to Alice's minting (as Series Issuer) of 100 subdivision of Token #1
      op = histories[10].op;
      BOOST_REQUIRE(op.is_type<nft_mint_operation>());
      mint_op = op.get<nft_mint_operation>();
      BOOST_CHECK(mint_op.issuer == alice_id);
      BOOST_CHECK(mint_op.asset_id == sub_asset_1_id);
      BOOST_CHECK_EQUAL(mint_op.subdivisions.value, 100);

      // The next operation should correspond to Alice's creation of Token #1 as a UIA sub-asset
      op = histories[11].op;
      BOOST_REQUIRE(op.is_type<asset_create_operation>());
      asset_create_op = op.get<asset_create_operation>();
      BOOST_CHECK(asset_create_op.issuer == alice_id);
      BOOST_CHECK(asset_create_op.symbol == sub_asset_1_name);
      BOOST_CHECK(asset_create_op.precision == 2); // 10^2 = 100 subdivision for a single unit

      // The next operation should correspond to Alice's creation of the Series
      op = histories[12].op;
      BOOST_REQUIRE(op.is_type<nft_series_create_operation>());
      nft_series_create_operation nft_series_create_op = op.get<nft_series_create_operation>();
      BOOST_CHECK(nft_series_create_op.issuer == alice_id);
      BOOST_CHECK(nft_series_create_op.asset_id == series_id);
      BOOST_CHECK(nft_series_create_op.manager == alice_id);
      BOOST_CHECK(nft_series_create_op.beneficiary == alice_id);

      // The next operation should correspond to Alice's creation of the Series UIA (parent asset)
      op = histories[13].op;
      BOOST_REQUIRE(op.is_type<asset_create_operation>());
      asset_create_op = op.get<asset_create_operation>();
      BOOST_CHECK(asset_create_op.issuer == alice_id);
      BOOST_CHECK(asset_create_op.symbol == series_name);

      // The next operation should correspond to the transfer of CORE to Alice
      op = histories[14].op;
      BOOST_REQUIRE(op.is_type<transfer_operation>());
      transfer_operation transfer_op = op.get<transfer_operation>();
      BOOST_CHECK(transfer_op.to == alice_id);
      BOOST_CHECK(transfer_op.amount == asset(100 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

      // The next operation should correspond to Alice's account creation
      op = histories[15].op;
      BOOST_REQUIRE(op.is_type<account_create_operation>());
      account_create_operation ac_op = op.get<account_create_operation>();
      BOOST_CHECK_EQUAL(ac_op.name, "alice");

   } FC_LOG_AND_RETHROW()
}


/**
 * Test sale of single-subdivision assets with and without market fees
 * to check for potential problems with secondary sales.
 *
 * NOTE: At 100% market fee, the buyer of the single-subdivision NFT will pay for the NFT
 * but will receive nothing because the market fee will completely consume what is being sold.
 * The NFT will then reside, according to db_market.pay_market_fees(), in the vesting balance of either:
 * the seller's registrar,
 * the seller's referrer, or
 * the asset's accumulated fees.
 */

/*
 * Test sales of single-subdivision assets WITHOUT market fees
 */
BOOST_AUTO_TEST_CASE( single_division_secondary_sales_with_0percent_market_fees ) {
   try {
      BOOST_TEST_MESSAGE("Testing secondary sales of single-subdivision assets with 0% market fee");
      uint16_t market_fee_percent = 0 * GRAPHENE_1_PERCENT;
      test_single_division_secondary_sales(market_fee_percent);

   } FC_LOG_AND_RETHROW()
}

/*
 * Test sales of single-subdivision assets WITH market fees
 */
BOOST_AUTO_TEST_CASE( single_division_secondary_sales_with_1percent_market_fee ) {
   try {
      BOOST_TEST_MESSAGE("Testing secondary sales of single-subdivision assets with 1% market fee");
      uint16_t market_fee_percent = 1 * GRAPHENE_1_PERCENT;
      test_single_division_secondary_sales(market_fee_percent);

   } FC_LOG_AND_RETHROW()
}

/*
 * Test sales of single-subdivision assets WITH market fees
 */
BOOST_AUTO_TEST_CASE( single_division_secondary_sales_with_99percent_market_fee ) {
   try {
      BOOST_TEST_MESSAGE("Testing secondary sales of single-subdivision assets with 99% market fee");
      uint16_t market_fee_percent = 99 * GRAPHENE_1_PERCENT;
      test_single_division_secondary_sales(market_fee_percent);

   } FC_LOG_AND_RETHROW()
}

/*
 * Test sales of single-subdivision assets WITH market fees
 */
BOOST_AUTO_TEST_CASE( single_division_secondary_sales_with_100percent_market_fee ) {
   try {
      BOOST_TEST_MESSAGE("Testing secondary sales of single-subdivision assets with 100% market fee");
      uint16_t market_fee_percent = 100 * GRAPHENE_1_PERCENT;
      test_single_division_secondary_sales(market_fee_percent);

   } FC_LOG_AND_RETHROW()
}

/**
 * Test secondary transfer of an NFT with a 0% royalty fee
 */
BOOST_AUTO_TEST_CASE(nft_2ndXfer_royalty_collection_a) {
   try {
      INVOKE(nft_primary_transfer_a);
      advance_past_m3_hardfork();

      // Initialize
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      ACTOR(doug);
      share_type alice_init_balance_core = get_balance(alice_id, core_id);
      share_type bob_init_balance_core = get_balance(bob_id, core_id);
      share_type charlie_init_balance_core = get_balance(charlie_id, core_id);
      share_type doug_init_balance_core = get_balance(doug_id, core_id);
      BOOST_REQUIRE_EQUAL(doug_init_balance_core.value, 0);

      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;
      graphene::chain::transfer_operation tx_op;

      // Verify everyone's balances of Token #1 at the beginning of the test
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 0);

      // Verify everyone's balances of Token #2 at the beginning of the test
      BOOST_REQUIRE_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_2_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1000);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_2_id), 0);

      ///
      /// Secondary transfer of an NFT
      /// 5 subdivision of NFT #1 will be transferred.
      /// With an RFP = 0% and a minimum price per subdivision of 0 CORE
      /// The expected royalty fee should equal 0 CORE.
      /// Therefore, the CORE balances should be unaffected
      ///
      asset amount1 = graphene::chain::asset(5, sub_asset_1_id);
      transfer(charlie_id, doug_id, amount1);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_1_id), 40 - 5);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_1_id), 5);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(nft_primary_transfer_d) {
   try {
      advance_past_m3_hardfork();

      // Initialize
      const asset_id_type core_id = asset_id_type();
      ACTORS((creatora)(creatorb)(mgra)(mgrb)(beneficiarya)(beneficiaryb)(treasurya)(alice)(bob)(charlie)(doug));
      int64_t init_balance(5000000 * GRAPHENE_BLOCKCHAIN_PRECISION);
      transfer(committee_account, creatora_id, graphene::chain::asset(init_balance));
      transfer(committee_account, mgra_id, graphene::chain::asset(init_balance));
      transfer(committee_account, treasurya_id, graphene::chain::asset(init_balance));
      transfer(committee_account, alice_id, graphene::chain::asset(init_balance));
      transfer(committee_account, bob_id, graphene::chain::asset(init_balance));

      share_type charlie_init_balance_core = get_balance(charlie_id, core_id);
      share_type doug_init_balance_core = get_balance(doug_id, core_id);
      BOOST_CHECK_EQUAL(charlie_init_balance_core.value, 0);
      BOOST_CHECK_EQUAL(doug_init_balance_core.value, 0);

      ///
      /// Create an NFT Series with a positive royalty
      ///
      const string series_a1_name = "SERIESA";
      uint16_t royalty_fee_centipercent = 5 * GRAPHENE_1_PERCENT;
      create_asset_and_series(series_a1_name, creatora_id,
                              creatora_private_key,
                              beneficiarya_id, mgra_id,
                              royalty_fee_centipercent);

      // Create an asset with a minimum price of 0 CORE per subdivision
      // and mint it into the Series
      const string sub_asset_1_name = "SERIESA.SUB1";
      asset req_backing_per_subdivision = asset(0, core_id);
      asset min_price_per_subdivision = asset(0, core_id);
      const uint16_t flags = DEFAULT_UIA_ASSET_ISSUER_PERMISSION ^ transfer_restricted; // Exclude transfer restrictions
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, 2,
                                                                     creatora_id, creatora_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision,
                                                                     flags);

      // Create an asset with a minimum price of 10 CORE per subdivision
      // and mint it into the Series
      const string sub_asset_2_name = "SERIESA.SUB2";
      req_backing_per_subdivision = asset(10, core_id);
      min_price_per_subdivision = asset(10, core_id);
      const asset_id_type sub_asset_2_id = create_sub_asset_and_mint(sub_asset_2_name, 2,
                                                                     creatora_id, creatora_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision,
                                                                     flags);

      // Create an asset with a minimum price of 10 CORE per subdivision
      // and mint it into the Series
      const string sub_asset_3_name = "SERIESA.SUB3";
      req_backing_per_subdivision = asset(25, core_id);
      min_price_per_subdivision = asset(75 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id);
      const asset_id_type sub_asset_3_id = create_sub_asset_and_mint(sub_asset_3_name, 2,
                                                                     creatora_id, creatora_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision,
                                                                     flags);

      // Primary transfer
      // Manager attempts to primary transfer 40 subdivisions (40%) of the token from the Inventory
      graphene::chain::nft_primary_transfer_operation ptx_op;

      BOOST_TEST_MESSAGE("Primary transfer of 40% of Token #1");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(40, sub_asset_1_id);
      ptx_op.to = alice_id;
      ptx_op.manager = mgra_id;
      // No provisioner is required for NFTs that do not require backing
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgra_private_key);
      PUSH_TX(db, trx);

      BOOST_TEST_MESSAGE("Primary transfer of 40% of Token #2");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(40, sub_asset_2_id);
      ptx_op.to = bob_id;
      ptx_op.manager = mgra_id;
      ptx_op.provisioner = treasurya_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgra_private_key);
      sign(trx, treasurya_private_key);
      PUSH_TX(db, trx);

      // Verify balances of Token #1
      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_1_id), 40);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_1_id), 0);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_1_id), 0);

      // Verify balances of Token #2
      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_2_id), 40);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_2_id), 0);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_2_id), 0);

      BOOST_TEST_MESSAGE("Primary transfer of 100% of Token #3");
      ptx_op = nft_primary_transfer_operation();
      ptx_op.amount = asset(100, sub_asset_3_id);
      ptx_op.to = bob_id;
      ptx_op.manager = mgra_id;
      ptx_op.provisioner = treasurya_id;
      trx.clear();
      trx.operations.push_back(ptx_op);
      sign(trx, mgra_private_key);
      sign(trx, treasurya_private_key);
      PUSH_TX(db, trx);

      // Verify everyone's balances of Token #1
      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_1_id), 40);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_1_id), 0);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_1_id), 0);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_1_id), 0);

      // Verify everyone's balances of Token #2
      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_2_id), 0);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_2_id), 40);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_2_id), 0);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_2_id), 0);

      // Verify everyone's balances of Token #3
      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_3_id), 0);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_3_id), 100);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_3_id), 0);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_3_id), 0);

   } FC_LOG_AND_RETHROW()
}

/**
 * Test secondary transfer of an NFT with a 5% royalty fee
 */
BOOST_AUTO_TEST_CASE(nft_2ndXfer_royalty_collection_d) {
   try {
      INVOKE(nft_primary_transfer_d);

      // Initialize
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      GET_ACTOR(doug);

      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const string sub_asset_3_name = series_name + ".SUB3";
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;
      const asset_id_type sub_asset_3_id = get_asset(sub_asset_3_name).id;

      share_type alice_init_balance_core = get_balance(alice_id, core_id);
      share_type bob_init_balance_core = get_balance(bob_id, core_id);
      share_type charlie_init_balance_core = get_balance(charlie_id, core_id);
      share_type doug_init_balance_core = get_balance(doug_id, core_id);
      BOOST_CHECK_EQUAL(charlie_init_balance_core.value, 0);
      BOOST_CHECK_EQUAL(doug_init_balance_core.value, 0);

      ///
      /// Secondary transfer of an NFT
      /// 5 subdivision of NFT #1 will be transferred.
      /// With an RFP = 5% and a minimum price per subdivision of 0 CORE
      /// The expected royalty fee should equal 0 CORE.
      /// Therefore, the CORE balances should be unaffected
      ///
      asset amount1 = graphene::chain::asset(5, sub_asset_1_id);
      transfer(alice_id, charlie_id, amount1);
      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_1_id), 40 - 5);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_1_id), 5);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      {
         auto token_itr = token_name_idx.find(sub_asset_1_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(0, core_id));
      }

      ///
      /// Secondary transfer of an NFT
      /// 5 subdivision of NFT #2 will be transferred.
      /// With an RFP = 5% and a minimum price of 10 CORE per subdivision
      /// The expected royalty fee should equal 5% * (50 CORE) = 2.5 CORE -> 3 CORE
      /// Therefore, the CORE balances should be affected.
      ///
      asset amount2 = graphene::chain::asset(5, sub_asset_2_id);
      transfer(bob_id, charlie_id, amount2);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_2_id), 40 - 5);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_2_id), 5);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value - 3);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      {
         auto token_itr = token_name_idx.find(sub_asset_2_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(3, core_id));
      }

      ///
      /// Secondary transfer of an NFT
      /// 5 subdivision of NFT #1 will be transferred **by an account with 0 CORE**.
      /// With an RFP = 5% and a minimum price per subdivision of 0 CORE
      /// The expected royalty fee should equal 0 CORE.
      /// Therefore, the CORE balances should be unaffected
      ///
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_1_id), 5);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_1_id), 0);
      transfer(charlie_id, doug_id, amount1); // Doug is the new recipient

      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_1_id), 40 - 5);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_1_id), 5 - 5);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_1_id), 5);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value - 3);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      {
         auto token_itr = token_name_idx.find(sub_asset_1_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(0, core_id));
      }

      ///
      /// Secondary transfer of an NFT
      /// 5 subdivision of NFT #2 will be attempt to be transferred
      /// **by an account with 0 CORE**.
      /// With an RFP = 5% and a minimum price of 10 CORE per subdivision
      /// The expected royalty fee should equal 5% * (50 CORE) = 2.5 CORE -> 3 CORE
      /// However the attempted transfer should be rejected due to the sender
      /// having insufficient funds to pay the royalty fee.
      ///
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, core_id), 0);
      BOOST_REQUIRE_EQUAL(get_balance(charlie_id, sub_asset_2_id), 5);
      BOOST_REQUIRE_EQUAL(get_balance(doug_id, sub_asset_2_id), 0);
      REQUIRE_EXCEPTION_WITH_TEXT(transfer(charlie_id, doug_id, amount2), "Insufficient Balance to pay NFT royalty");

      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_2_id), 40 - 5);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_2_id), 5);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_2_id), 0);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value - 3);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      {
         auto token_itr = token_name_idx.find(sub_asset_2_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(3, core_id));
      }

      ///
      /// ANOTHER Secondary transfer of an NFT SUB2
      /// 20 subdivision of NFT #2 will be transferred.
      /// With an RFP = 5% and a minimum price of 10 CORE per subdivision
      /// The expected royalty fee should equal 5% * (200 CORE) = 10 CORE
      /// Therefore, the CORE balances should be affected.
      ///
      asset amount4 = graphene::chain::asset(20, sub_asset_2_id);
      transfer(bob_id, doug_id, amount4);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_2_id), 40 - 5 - 20);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_2_id), 20);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value - 3 - 10);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      {
         auto token_itr = token_name_idx.find(sub_asset_2_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(3 + 10, core_id));
      }


      ///
      /// ANOTHER Secondary transfer of an NFT SUB3
      /// 40 subdivision of NFT #3 will be transferred.
      /// With an RFP = 5% and a minimum price of 75 * GRAPHENE_BLOCKCHAIN_PRECISION CORE per subdivision
      /// The expected royalty fee should equal 5% * (3000 * GRAPHENE_BLOCKCHAIN_PRECISION CORE) = 150 * GRAPHENE_BLOCKCHAIN_PRECISION CORE
      /// Therefore, the CORE balances should be affected.
      ///
      asset amount5 = graphene::chain::asset(40, sub_asset_3_id);
      transfer(bob_id, doug_id, amount5);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_3_id), 100 - 40);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_3_id), 40);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value - 3 - 10 - (150 * GRAPHENE_BLOCKCHAIN_PRECISION));
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      {
         auto token_itr = token_name_idx.find(sub_asset_3_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(150 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));
      }


      // To permit the most recent account history to embed itself into a block
      // and thereby become accessible in account history
      generate_block();
      graphene::app::history_api hist_api(app);

      ///
      /// Inspect Alice's account history
      ///
      vector <operation_history_object> histories;
      histories = hist_api.get_account_history("alice");
      int count = histories.size();
      // Alices's history in this scenario should include:
      // 1 Account creation
      // 1 Transfer of CORE to Alice
      // 1 Primary Transfer of 40 subdivisions of SERIESA.SUB1 to Alice
      // 1 Secondary Transfer of 5 subdivisions of SERIESA.SUB1 to Charlie
      BOOST_CHECK_EQUAL(count, 4);

      operation op;

      // Account histories are sorted in decreasing time order
      // The first operation should correspond to Alice's transfer to Charlie
      op = histories[0].op;
      BOOST_REQUIRE(op.is_type<transfer_operation>());
      transfer_operation tx_op = op.get<transfer_operation>();
      BOOST_CHECK(tx_op.from == alice_id);
      BOOST_CHECK(tx_op.to == charlie_id);
      BOOST_CHECK(tx_op.amount == asset(5, sub_asset_1_id));

      // The next operation should correspond to the primary transfer to Alice
      op = histories[1].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      nft_primary_transfer_operation ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == alice_id);
      BOOST_CHECK(ptx_op.amount == asset(40, sub_asset_1_id));


      ///
      /// Inspect Bob's account history
      ///
      histories = hist_api.get_account_history("bob");
      count = histories.size();
      // Bob's history in this scenario should include:
      // 1 Account creation
      // 1 Transfer of CORE to Bob
      // 1 Primary Transfer of 40 subdivisions of SERIESA.SUB2 to Alice
      // 1 Primary Transfer of 100 subdivisions of SERIESA.SUB3 to Alice
      // 1 Secondary Transfer of 5 subdivisions of SERIESA.SUB2 to Charlie
      // 1 virtual royalty payment of Secondary of 3 subdivisions of CORE to the Series reservoir
      // 1 Secondary Transfer of 20 subdivisions of SERIESA.SUB2 to Doug
      // 1 virtual royalty payment of Secondary of 10 subdivisions of CORE to the Series reservoir
      // 1 Secondary Transfer of 40 subdivisions of SERIESA.SUB3 to Doug
      // 1 virtual royalty payment of Secondary of 150 * GRAPHENE_BLOCKCHAIN_PRECISION subdivisions of CORE to the Series reservoir
      BOOST_CHECK_EQUAL(count, 10);

      // Account histories are sorted in decreasing time order
      // The first operation should correspond to Bob's virtual royalty payment
      op = histories[0].op;
      BOOST_REQUIRE(op.is_type<nft_royalty_paid_operation>());
      nft_royalty_paid_operation roy_op = op.get<nft_royalty_paid_operation>();
      BOOST_CHECK(roy_op.tx_amount == asset(40, sub_asset_3_id));
      BOOST_CHECK(roy_op.payer == bob_id);
      BOOST_CHECK(roy_op.royalty == asset(150 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

      // The next operation should correspond to Bob's secondary transfer to Doug
      op = histories[1].op;
      BOOST_REQUIRE(op.is_type<transfer_operation>());
      tx_op = op.get<transfer_operation>();
      BOOST_CHECK(tx_op.from == bob_id);
      BOOST_CHECK(tx_op.to == doug_id);
      BOOST_CHECK(tx_op.amount == asset(40, sub_asset_3_id));

      // The next operation should correspond to Bob's virtual royalty payment
      op = histories[2].op;
      BOOST_REQUIRE(op.is_type<nft_royalty_paid_operation>());
      roy_op = op.get<nft_royalty_paid_operation>();
      BOOST_CHECK(roy_op.tx_amount == asset(20, sub_asset_2_id));
      BOOST_CHECK(roy_op.payer == bob_id);
      BOOST_CHECK(roy_op.royalty == asset(10, core_id));

      // The next operation should correspond to Bob's secondary transfer to Doug
      op = histories[3].op;
      BOOST_REQUIRE(op.is_type<transfer_operation>());
      tx_op = op.get<transfer_operation>();
      BOOST_CHECK(tx_op.from == bob_id);
      BOOST_CHECK(tx_op.to == doug_id);
      BOOST_CHECK(tx_op.amount == asset(20, sub_asset_2_id));

      // The next operation should correspond to Bob's virtual royalty payment
      op = histories[4].op;
      BOOST_REQUIRE(op.is_type<nft_royalty_paid_operation>());
      roy_op = op.get<nft_royalty_paid_operation>();
      BOOST_CHECK(roy_op.tx_amount == asset(5, sub_asset_2_id));
      BOOST_CHECK(roy_op.payer == bob_id);
      BOOST_CHECK(roy_op.royalty == asset(3, core_id));

      // The next operation should correspond to Bob's secondary transfer to Charlie
      op = histories[5].op;
      BOOST_REQUIRE(op.is_type<transfer_operation>());
      tx_op = op.get<transfer_operation>();
      BOOST_CHECK(tx_op.from == bob_id);
      BOOST_CHECK(tx_op.to == charlie_id);
      BOOST_CHECK(tx_op.amount == asset(5, sub_asset_2_id));

      // The next operation should correspond to the primary transfer to Bob of Token #3
      op = histories[6].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == bob_id);
      BOOST_CHECK(ptx_op.amount == asset(100, sub_asset_3_id));

      // The next operation should correspond to the primary transfer to Bob of Token #2
      op = histories[7].op;
      BOOST_REQUIRE(op.is_type<nft_primary_transfer_operation>());
      ptx_op = op.get<nft_primary_transfer_operation>();
      BOOST_CHECK(ptx_op.to == bob_id);
      BOOST_CHECK(ptx_op.amount == asset(40, sub_asset_2_id));

   } FC_LOG_AND_RETHROW()
}

/**
 * Test "tiny" secondary transfer of an NFT with a 5% royalty fee
 * which should result in a royalty payment that is rounded up to 1 CORE
 */
BOOST_AUTO_TEST_CASE(nft_2ndXfer_royalty_collection_tiny) {
   try {
      INVOKE(nft_primary_transfer_d);

      // Initialize
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      GET_ACTOR(doug);

      const string series_name = "SERIESA";
      const string sub_asset_1_name = series_name + ".SUB1";
      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type sub_asset_1_id = get_asset(sub_asset_1_name).id;
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;

      share_type alice_init_balance_core = get_balance(alice_id, core_id);
      share_type bob_init_balance_core = get_balance(bob_id, core_id);
      share_type charlie_init_balance_core = get_balance(charlie_id, core_id);
      share_type doug_init_balance_core = get_balance(doug_id, core_id);
      BOOST_CHECK_EQUAL(charlie_init_balance_core.value, 0);
      BOOST_CHECK_EQUAL(doug_init_balance_core.value, 0);

      ///
      /// Secondary transfer of an NFT
      /// 1 subdivision of NFT #1 will be transferred.
      /// With an RFP = 5% and a minimum price per subdivision of 0 CORE
      /// The expected royalty fee should equal 0 CORE.
      /// Therefore, the CORE balances should be unaffected
      ///
      asset amount1 = graphene::chain::asset(1, sub_asset_1_id);
      transfer(alice_id, charlie_id, amount1);
      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_1_id), 40 - 1);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_1_id), 1);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      {
         auto token_itr = token_name_idx.find(sub_asset_1_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(0, core_id));
      }

      ///
      /// Secondary transfer of an NFT
      /// 1 subdivision of NFT #2 will be transferred.
      /// With an RFP = 5% and a minimum price of 10 CORE per subdivision
      /// The expected royalty fee should equal 5% * (10 CORE) = 0.5 CORE -> 1 CORE
      /// Therefore, the CORE balances should be affected.
      ///
      asset amount2 = graphene::chain::asset(1, sub_asset_2_id);
      transfer(bob_id, charlie_id, amount2);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_2_id), 40 - 1);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_2_id), 1);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value - 1);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      {
         auto token_itr = token_name_idx.find(sub_asset_2_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(1, core_id));
      }

   } FC_LOG_AND_RETHROW()
}

/**
 * Test secondary transfer of an NFT with a 5% royalty fee
 * and large minimum price that requires a royalty payment
 * exceeding the supply of the core token.
 */
BOOST_AUTO_TEST_CASE(nft_2ndXfer_royalty_collection_large) {
   try {
      INVOKE(nft_primary_transfer_d);

      // Initialize
      const asset_id_type core_id = asset_id_type();
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      GET_ACTOR(doug);

      const string series_name = "SERIESA";
      const string sub_asset_3_name = series_name + ".SUB3";
      const asset_id_type sub_asset_3_id = get_asset(sub_asset_3_name).id;

      share_type alice_init_balance_core = get_balance(alice_id, core_id);
      share_type bob_init_balance_core = get_balance(bob_id, core_id);
      share_type charlie_init_balance_core = get_balance(charlie_id, core_id);
      share_type doug_init_balance_core = get_balance(doug_id, core_id);
      BOOST_REQUIRE_EQUAL(charlie_init_balance_core.value, 0);
      BOOST_REQUIRE_EQUAL(doug_init_balance_core.value, 0);

      ///
      /// Secondary transfer of an NFT
      /// 100 subdivision of NFT #3 will be transferred.
      /// With an RFP = 5% and a minimum price per subdivision of 75 * GRAPHENE_BLOCKCHAIN_PRECISION CORE
      /// The expected royalty fee should equal 375 * GRAPHENE_BLOCKCHAIN_PRECISION CORE.
      /// Therefore, the CORE balances should be unaffected
      ///
      BOOST_REQUIRE_EQUAL(get_balance(bob_id, sub_asset_3_id), 100);
      asset amount3 = graphene::chain::asset(100, sub_asset_3_id);
      transfer(bob_id, charlie_id, amount3);
      BOOST_CHECK_EQUAL(get_balance(alice_id, sub_asset_3_id), 100 - 100);
      BOOST_CHECK_EQUAL(get_balance(charlie_id, sub_asset_3_id), 100);

      BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_init_balance_core.value - (375 * GRAPHENE_BLOCKCHAIN_PRECISION));
      BOOST_CHECK_EQUAL(get_balance(charlie_id, core_id), charlie_init_balance_core.value);
      BOOST_CHECK_EQUAL(get_balance(doug_id, core_id), doug_init_balance_core.value);

      // Verify the implementation object
      const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
      {
         auto token_itr = token_name_idx.find(sub_asset_3_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(375 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));
      }

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(nft_history_single_transaction) {
   try
   {
      INVOKE(nft_2ndXfer_royalty_collection_large);

      // Initialize
      const asset_id_type core_id = asset_id_type();

      // Enable the plug-in
      BOOST_TEST_MESSAGE("Enabling NFT History plug-in");
      app.enable_plugin("nft_history");
      graphene::nft_history::nft_history nft_history(app);

      // Advance the block to receive the latest transactions into the plug-in
      generate_block();

      // Check the collected royalty
      const string series_name = "SERIESA";
      const string sub_asset_3_name = series_name + ".SUB3";
      const asset_id_type sub_asset_3_id = get_asset(sub_asset_3_name).id;
      asset royalty = nft_history.get_royalty_reservoir_by_token(sub_asset_3_id, false);
      BOOST_CHECK(royalty == asset(375 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));
      royalty = nft_history.get_royalty_reservoir_by_token(sub_asset_3_name, false);
      BOOST_CHECK(royalty == asset(375 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(nft_history_multiple_transaction) {
   try
   {
      const fc::time_point_sec INITIAL_TIME = db.head_block_time();
      INVOKE(nft_2ndXfer_royalty_collection_d);

      // Initialize
      const asset_id_type core_id = asset_id_type();

      // Enable the plug-in
      BOOST_TEST_MESSAGE("Enabling NFT History plug-in");
      app.enable_plugin("nft_history");
      graphene::nft_history::nft_history nft_history(app);

      // Advance the block to incorporate the latest transactions into a block and thereby into the plug-in
      const fc::time_point_sec BEFORE_STAGE_1_END_TIME = db.head_block_time();
      generate_block();

      const fc::time_point_sec STAGE_1_END_TIME = db.head_block_time();
      const fc::time_point_sec STAGE_2_START_TIME = STAGE_1_END_TIME;

      // Check the collected royalty
      const string series_name = "SERIESA";
      const asset_id_type series_id = get_asset(series_name).id;

      const string sub_asset_2_name = series_name + ".SUB2";
      const asset_id_type sub_asset_2_id = get_asset(sub_asset_2_name).id;

      const string sub_asset_3_name = series_name + ".SUB3";
      const asset_id_type sub_asset_3_id = get_asset(sub_asset_3_name).id;

      // Check the royalty reservoir by token
      asset royalty = nft_history.get_royalty_reservoir_by_token(sub_asset_3_id, false);
      BOOST_CHECK(royalty == asset(150 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));
      royalty = nft_history.get_royalty_reservoir_by_token(sub_asset_3_name, false);
      BOOST_CHECK(royalty == asset(150 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));
      time_point_sec start = INITIAL_TIME;
      time_point_sec end = time_point_sec::maximum();
      royalty = nft_history.get_royalties_collected_by_token(sub_asset_3_name, start, end);
      BOOST_CHECK(royalty == asset(150 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

      // Check the royalty reservoir by token
      royalty = nft_history.get_royalty_reservoir_by_token(sub_asset_2_id, false);
      BOOST_CHECK(royalty == asset(3 + 10, core_id));
      royalty = nft_history.get_royalty_reservoir_by_token(sub_asset_2_name, false);
      BOOST_CHECK(royalty == asset(3 + 10, core_id));
      royalty = nft_history.get_royalties_collected_by_token(sub_asset_2_name, start, end);
      BOOST_CHECK(royalty == asset(3 + 10, core_id));

      // Check the royalty reservoir by series
      vector<asset> royalties = nft_history.get_royalty_reservoir_by_series(series_name, false);
      BOOST_CHECK_EQUAL(royalties.size(), 1); // Only a single asset type
      BOOST_CHECK(royalties[0] == asset(3 + 10 + (150 * GRAPHENE_BLOCKCHAIN_PRECISION), core_id));
      royalties = nft_history.get_royalty_reservoir_by_series(series_id, false);
      BOOST_CHECK_EQUAL(royalties.size(), 1); // Only a single asset type
      BOOST_CHECK(royalties[0] == asset(3 + 10 + (150 * GRAPHENE_BLOCKCHAIN_PRECISION), core_id));


      ///
      /// Stage 2 of transfer activity to trigger royalty payments
      ///
      GET_ACTOR(alice);
      GET_ACTOR(bob);
      GET_ACTOR(charlie);
      GET_ACTOR(doug);

      // Advance the blockchain
      generate_blocks(5);

      /// Perform additional transfers that should generate royalties
      ///
      /// ANOTHER Secondary transfer of an NFT SUB2
      /// 10 subdivision of NFT #2 will be transferred.
      /// With an RFP = 5% and a minimum price of 10 CORE per subdivision
      /// The expected royalty fee should equal 5% * (100 CORE) = 5 CORE
      /// Therefore, the CORE balances should be affected.
      ///
      asset amount1 = graphene::chain::asset(10, sub_asset_2_id);
      transfer(bob_id, doug_id, amount1);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_2_id), 40 - 5 - 20 - 10);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_2_id), 20 + 10);

      // Verify the implementation object
      {
         const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
         auto token_itr = token_name_idx.find(sub_asset_2_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset(3 + 10 + 5, core_id));
      }

      ///
      /// ANOTHER Secondary transfer of an NFT SUB3
      /// 20 subdivision of NFT #3 will be transferred.
      /// With an RFP = 5% and a minimum price of 75 * GRAPHENE_BLOCKCHAIN_PRECISION CORE per subdivision
      /// The expected royalty fee should equal 5% * (1500 * GRAPHENE_BLOCKCHAIN_PRECISION CORE) = 75 * GRAPHENE_BLOCKCHAIN_PRECISION CORE
      /// Therefore, the CORE balances should be affected.
      ///
      asset amount2 = graphene::chain::asset(20, sub_asset_3_id);
      transfer(bob_id, doug_id, amount2);
      BOOST_CHECK_EQUAL(get_balance(bob_id, sub_asset_3_id), 100 - 40 - 20);
      BOOST_CHECK_EQUAL(get_balance(doug_id, sub_asset_3_id), 40 + 20);

      // Verify the implementation object
      {
         const auto &token_name_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
         auto token_itr = token_name_idx.find(sub_asset_3_name);
         BOOST_REQUIRE(token_itr != token_name_idx.end());
         const nft_token_object &token_obj = *token_itr;
         BOOST_CHECK(token_obj.royalty_reservoir == asset((150 + 75) * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));
      }

      // Advance the block to incorporate the latest transactions into a block and thereby the plug-in
      generate_block();

      // Advance to conclude Stage 2
      generate_block();
      const fc::time_point_sec STAGE_2_END_TIME = db.head_block_time();

      ///
      /// Check royalty collected across different timespans
      ///

      /// All time
      start = INITIAL_TIME;
      end = time_point_sec::maximum();

      // Check the cumulative royalty by token
      royalty = nft_history.get_royalties_collected_by_token(sub_asset_3_name, start, end);
      BOOST_CHECK(royalty == asset((150 + 75) * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

      royalty = nft_history.get_royalties_collected_by_token(sub_asset_2_name, start, end);
      BOOST_CHECK(royalty == asset(3 + 10 + 5, core_id));

      // Check the cumulative royalty by series
      royalties = nft_history.get_royalties_by_series(series_name, start, end);
      BOOST_CHECK_EQUAL(royalties.size(), 1); // Only a single asset type
      BOOST_CHECK(royalties[0] == asset(3 + 10 + 5 + ((150 + 75) * GRAPHENE_BLOCKCHAIN_PRECISION), core_id));

      /// From blockchain start time to just before Stage 1 activities are recorded in a block
      start = INITIAL_TIME;
      end = BEFORE_STAGE_1_END_TIME;

      // Check the cumulative royalty by token
      royalty = nft_history.get_royalties_collected_by_token(sub_asset_3_name, start, end);
      BOOST_CHECK(royalty == asset(0 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

      royalty = nft_history.get_royalties_collected_by_token(sub_asset_2_name, start, end);
      BOOST_CHECK(royalty == asset(0, core_id));

      // Check the cumulative royalty by series
      royalties = nft_history.get_royalties_by_series(series_name, start, end);
      BOOST_CHECK_EQUAL(royalties.size(), 0); // No royalties

      /// From blockchain start time to Stage 1 end-time
      start = INITIAL_TIME;
      end = STAGE_1_END_TIME;

      // Check the cumulative royalty by token
      royalty = nft_history.get_royalties_collected_by_token(sub_asset_3_name, start, end);
      BOOST_CHECK(royalty == asset((150) * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

      royalty = nft_history.get_royalties_collected_by_token(sub_asset_2_name, start, end);
      BOOST_CHECK(royalty == asset(3 + 10, core_id));

      // Check the cumulative royalty by series
      royalties = nft_history.get_royalties_by_series(series_name, start, end);
      BOOST_CHECK_EQUAL(royalties.size(), 1); // Only a single asset type
      BOOST_CHECK(royalties[0] == asset(3 + 10 + (150 * GRAPHENE_BLOCKCHAIN_PRECISION), core_id));

      /// From Stage 2 start to Stage 2 end
      start = STAGE_2_START_TIME;
      end = STAGE_2_END_TIME;

      // Check the cumulative royalty by token
      royalty = nft_history.get_royalties_collected_by_token(sub_asset_3_name, start, end);
      BOOST_CHECK(royalty == asset(75 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

      royalty = nft_history.get_royalties_collected_by_token(sub_asset_2_name, start, end);
      BOOST_CHECK(royalty == asset(5, core_id));

      // Check the cumulative royalty by series
      royalties = nft_history.get_royalties_by_series(series_name, start, end);
      BOOST_CHECK_EQUAL(royalties.size(), 1); // Only a single asset type
      BOOST_CHECK(royalties[0] == asset(5 + (75 * GRAPHENE_BLOCKCHAIN_PRECISION), core_id));

      /// From after Stage 2 to the end times
      start = STAGE_2_END_TIME;
      end = time_point_sec::maximum();

      // Check the cumulative royalty by token
      royalty = nft_history.get_royalties_collected_by_token(sub_asset_3_name, start, end);
      BOOST_CHECK(royalty == asset(0 * GRAPHENE_BLOCKCHAIN_PRECISION, core_id));

      royalty = nft_history.get_royalties_collected_by_token(sub_asset_2_name, start, end);
      BOOST_CHECK(royalty == asset(0, core_id));

      // Check the cumulative royalty by series
      royalties = nft_history.get_royalties_by_series(series_name, start, end);
      BOOST_CHECK_EQUAL(royalties.size(), 0); // No royalties

   } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()

// Test sales of single-subdivision assets subject to a particular market fee percentage
void nft_database_fixture::test_single_division_secondary_sales(uint16_t market_fee_percent) {
   advance_past_m1_hardfork(); // Get past most hardforks

   // Initialize
   ACTORS((issuer)(alice)(bob)(charlie));
   int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);
   transfer(committee_account, issuer_id, graphene::chain::asset(init_balance));
   transfer(committee_account, alice_id, graphene::chain::asset(init_balance));
   transfer(committee_account, bob_id, graphene::chain::asset(init_balance));
   transfer(committee_account, charlie_id, graphene::chain::asset(init_balance));
   const asset_id_type core_id = asset_id_type();

   // Issuer creates an asset
   const string asset_name = "UNO";
   const uint8_t asset_precision = 0; // 10^0 = 1.  There can be only one!
   const uint64_t &whole_token_subdivisions = asset::scaled_precision(asset_precision).value;
   const uint64_t &max_supply = whole_token_subdivisions;
   const uint16_t flags = charge_market_fee;
   const asset_object &asset_obj = create_uia(asset_name, issuer_id(db), flags,
                                              max_supply, asset_precision, market_fee_percent);
   const asset_id_type asset_id = asset_obj.id;

   // Simple check of the market fee
   if (market_fee_percent == GRAPHENE_100_PERCENT) {
      BOOST_CHECK_EQUAL(1, db.calculate_market_fee(asset_obj, asset_obj.amount(1), true).amount.value);
   } else {
      BOOST_CHECK_EQUAL(0, db.calculate_market_fee(asset_obj, asset_obj.amount(1), true).amount.value);
   }
   BOOST_CHECK(asset_id == db.calculate_market_fee(asset_obj, asset_obj.amount(1), true).asset_id);

   if (market_fee_percent == GRAPHENE_100_PERCENT) {
      BOOST_CHECK_EQUAL(1, db.calculate_market_fee(asset_obj, asset_obj.amount(1), false).amount.value);
   } else {
      BOOST_CHECK_EQUAL(0, db.calculate_market_fee(asset_obj, asset_obj.amount(1), false).amount.value);
   }
   BOOST_CHECK(asset_id == db.calculate_market_fee(asset_obj, asset_obj.amount(1), true).asset_id);

   // Issue an amount into existence
   asset_issue_operation issue_op;
   issue_op.issuer = issuer_id;
   issue_op.asset_to_issue = asset_obj.amount(1);
   issue_op.issue_to_account = alice_id;
   trx.clear();
   trx.operations.push_back(issue_op);
   sign(trx, issuer_private_key);
   PUSH_TX(db, trx);
   // Verify balances
   BOOST_REQUIRE_EQUAL(get_balance(alice_id, asset_id), 1);

   ///
   /// SALE #1
   /// Alice attempts to sell it before Bob offers to buy it
   ///
   // Alice offers it for sale on the orderbook
   const int64_t alice_core_balance_0 = get_balance(alice_id, core_id);
   const time_point_sec order_expiration = time_point_sec::maximum();
   limit_order_create_operation alice_sell_op = create_sell_operation(alice_id,
                                                                      asset(1, asset_id),
                                                                      asset(100, core_id),
                                                                      order_expiration);
   trx.clear();
   trx.operations.push_back(alice_sell_op);
   sign(trx, alice_private_key);
   processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
   limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();
   // Verify balances
   // Alice's balance of the asset should be zero because it should be present on the orderbook
   BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_core_balance_0); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(alice_id, asset_id), 0);

   // Bob offers it for sale on the orderbook
   const int64_t bob_core_balance_0 = get_balance(bob_id, core_id);
   limit_order_create_operation bob_sell_op = create_sell_operation(bob_id,
                                                                    asset(100, core_id),
                                                                    asset(1, asset_id),
                                                                    order_expiration);
   trx.clear();
   trx.operations.push_back(bob_sell_op);
   sign(trx, bob_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   limit_order_id_type bob_order_id = ptx.operation_results[0].get<object_id_type>();

   // The orders should have been matched
   // Verify new balances
   BOOST_CHECK_EQUAL(get_balance(alice_id, asset_id), 0);
   if (market_fee_percent == GRAPHENE_100_PERCENT) {
      /*
       * NOTE: At 100% market fee, the buyer of the single-subdivision NFT will pay for the NFT
       * but will receive nothing because the market fee will completely consume what is being sold.
       * The NFT will then reside, according to db_market.pay_market_fees(), in the vesting balance of either:
       * the seller's registrar,
       * the seller's referrer, or
       * the asset's accumulated fees.
       */
      BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 0);
   } else {
      BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 1);
   }

   BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_core_balance_0 + 100); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_core_balance_0 - 100); // Assuming zero-fee operations


   if (market_fee_percent == GRAPHENE_100_PERCENT) {
      // Check accumulated fees
      const asset_dynamic_data_object& asset_dynamic_data = asset_obj.dynamic_asset_data_id(db);
      BOOST_CHECK_EQUAL(asset_dynamic_data.accumulated_fees.value, 1);

      // Cease testing as the NFT is no longer held in any account
      return;
   }

   ///
   /// SALE #2
   /// Alice attempts to buy it back before Bob offers it for sale
   ///
   // Advance to the next block
   generate_block();
   trx.clear();
   set_expiration(db, trx);

   // Alice offers to buy it from the orderbook
   const int64_t alice_core_balance_1 = get_balance(alice_id, core_id);
   alice_sell_op = create_sell_operation(alice_id,
                                         asset(100, core_id),
                                         asset(1, asset_id),
                                         order_expiration);
   trx.clear();
   trx.operations.push_back(alice_sell_op);
   sign(trx, alice_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   alice_order_id = ptx.operation_results[0].get<object_id_type>();
   // Verify balances
   // Alice's balance of the asset should be zero because it should be present on the orderbook
   BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_core_balance_1 - 100); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(alice_id, asset_id), 0);

   // Bob offers it for sale on the orderbook
   const int64_t bob_core_balance_1 = get_balance(bob_id, core_id);
   bob_sell_op = create_sell_operation(bob_id,
                                       asset(1, asset_id),
                                       asset(100, core_id),
                                       order_expiration);
   trx.clear();
   trx.operations.push_back(bob_sell_op);
   sign(trx, bob_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   bob_order_id = ptx.operation_results[0].get<object_id_type>();

   // The orders should have been matched
   // Verify new balances
   BOOST_CHECK_EQUAL(get_balance(alice_id, asset_id), 1);
   BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 0);

   BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_core_balance_1 - 100); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_core_balance_1 + 100); // Assuming zero-fee operations


   ///
   /// SALE #3: Same as SALE #1 except for the sequence of orders
   /// Alice attempts to buy it after Bob offers to buy it
   ///
   // Advance to the next block
   generate_block();
   trx.clear();
   set_expiration(db, trx);

   // Bob offers to buy it from the orderbook
   const int64_t bob_core_balance_2 = get_balance(bob_id, core_id);
   bob_sell_op = create_sell_operation(bob_id,
                                       asset(100, core_id),
                                       asset(1, asset_id),
                                       order_expiration);
   trx.clear();
   trx.operations.push_back(bob_sell_op);
   sign(trx, bob_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   bob_order_id = ptx.operation_results[0].get<object_id_type>();

   // Verify balances
   // Bob's balance of the asset should be zero because it should be present on the orderbook
   BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_core_balance_2 - 100); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 0);

   // Alice offers to sell it on the orderbook
   const int64_t alice_core_balance_2 = get_balance(alice_id, core_id);
   alice_sell_op = create_sell_operation(alice_id,
                                         asset(1, asset_id),
                                         asset(100, core_id),
                                         order_expiration);
   trx.clear();
   trx.operations.push_back(alice_sell_op);
   sign(trx, alice_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   alice_order_id = ptx.operation_results[0].get<object_id_type>();

   // The orders should have been matched
   // Verify new balances
   BOOST_CHECK_EQUAL(get_balance(alice_id, asset_id), 0);
   BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 1);

   // 100 CORE subdivisions should have been paid
   BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_core_balance_2 + 100); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_core_balance_2 - 100); // Assuming zero-fee operations


   ///
   /// SALE #4: Similar to SALE #2
   /// Check whether the maker's offer is selected over the taker's offer
   ///
   // Advance to the next block
   generate_block();
   trx.clear();
   set_expiration(db, trx);

   // Maker: Bob offers to sell it from the orderbook
   const int64_t bob_core_balance_3 = get_balance(bob_id, core_id);
   bob_sell_op = create_sell_operation(bob_id,
                                       asset(1, asset_id),
                                       asset(100, core_id),
                                       order_expiration);
   trx.clear();
   trx.operations.push_back(bob_sell_op);
   sign(trx, bob_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   bob_order_id = ptx.operation_results[0].get<object_id_type>();

   // Verify balances
   // Bob's balance of the asset should be zero because it should be present on the orderbook
   BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_core_balance_3); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 0);

   // Taker: Alice offers to buy it on the orderbook offering to OVERPAY by 50 CORE
   const int64_t alice_core_balance_3 = get_balance(alice_id, core_id);
   alice_sell_op = create_sell_operation(alice_id,
                                         asset(150, core_id),
                                         asset(1, asset_id),
                                         order_expiration);
   trx.clear();
   trx.operations.push_back(alice_sell_op);
   sign(trx, alice_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   alice_order_id = ptx.operation_results[0].get<object_id_type>();

   // The orders should have been matched
   // Verify new balances
   BOOST_CHECK_EQUAL(get_balance(alice_id, asset_id), 1);
   BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 0);

   // 100 CORE subdivisions should have been paid in favor of the taker
   BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_core_balance_3 - 100); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_core_balance_3 + 100); // Assuming zero-fee operations

   ///
   /// SALE #5: Similar to SALE #4
   /// Check whether the maker's offer is selected over the taker's offer
   ///
   // Advance to the next block
   generate_block();
   trx.clear();
   set_expiration(db, trx);

   // Maker: Bob offers to buy it from the orderbook
   const int64_t bob_core_balance_4 = get_balance(bob_id, core_id);
   bob_sell_op = create_sell_operation(bob_id,
                                       asset(100, core_id),
                                       asset(1, asset_id),
                                       order_expiration);
   trx.clear();
   trx.operations.push_back(bob_sell_op);
   sign(trx, bob_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   bob_order_id = ptx.operation_results[0].get<object_id_type>();

   // Verify balances
   // Bob's balance of the asset should be zero because it should be present on the orderbook
   BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_core_balance_4 - 100); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 0);

   // Taker: Alice offers to sell it on the orderbook by asking for only 75% of what is being asked
   const int64_t alice_core_balance_4 = get_balance(alice_id, core_id);
   alice_sell_op = create_sell_operation(alice_id,
                                         asset(1, asset_id),
                                         asset(75, core_id),
                                         order_expiration);
   trx.clear();
   trx.operations.push_back(alice_sell_op);
   sign(trx, alice_private_key);
   ptx = PUSH_TX(db, trx); // No exception should be thrown
   alice_order_id = ptx.operation_results[0].get<object_id_type>();

   // The orders should have been matched
   // Verify new balances
   BOOST_CHECK_EQUAL(get_balance(alice_id, asset_id), 0);
   BOOST_CHECK_EQUAL(get_balance(bob_id, asset_id), 1);

   // 100 CORE subdivisions should have been paid in favor of the taker
   BOOST_CHECK_EQUAL(get_balance(alice_id, core_id), alice_core_balance_4 + 100); // Assuming zero-fee operations
   BOOST_CHECK_EQUAL(get_balance(bob_id, core_id), bob_core_balance_4 - 100); // Assuming zero-fee operations

}
