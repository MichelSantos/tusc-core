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

#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/balance_object.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( nft_tests, database_fixture
)

void advance_past_nft_m1_hardfork(database_fixture *db_fixture) {
   db_fixture->generate_blocks(HARDFORK_NFT_M1_TIME);
   set_expiration(db_fixture->db, db_fixture->trx);
}

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
      advance_past_nft_m1_hardfork(this);
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
      advance_past_nft_m1_hardfork(this);
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
      // TODO [6] Switch to inspection through the database API
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
      // TODO [6] Switch to inspection through the database API
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
      BOOST_REQUIRE_EQUAL(token_obj.amount_in_inventory.value, 55);
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

      BOOST_REQUIRE_EQUAL(token_obj.amount_in_inventory.value, 85);
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

      BOOST_REQUIRE_EQUAL(token_obj.amount_in_inventory.value, 85);
      BOOST_REQUIRE_EQUAL(sub_asset_4_dd.current_supply.value, 85);

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

      BOOST_REQUIRE_EQUAL(token_obj.amount_in_inventory.value, 100);
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

      BOOST_REQUIRE_EQUAL(token_obj.amount_in_inventory.value, 100);
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

BOOST_AUTO_TEST_SUITE_END()
