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

#include <graphene/chain/balance_object.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;


struct nft_database_fixture : database_fixture {
   nft_database_fixture()
      : database_fixture() {
   }

   void advance_past_m1_hardfork() {
      generate_blocks(HARDFORK_NFT_M1_TIME);
      set_expiration(db, trx);
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
                                                 asset req_backing_per_subdivision, asset min_price_per_subdivision) {
      // Create the sub-asset
      const uint64_t& whole_token_subdivisions = asset::scaled_precision(sub_asset_precision).value;
      const uint64_t& max_supply = whole_token_subdivisions;
      const uint16_t flags = DEFAULT_UIA_ASSET_ISSUER_PERMISSION;
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
};


BOOST_FIXTURE_TEST_SUITE( nft_tests, nft_database_fixture
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
      BOOST_TEST_MESSAGE("Primary transfer of 40% of Token #1");
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
      // Verify everyone's balances before the primary transfer
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
      // Verify everyone's balances before the primary transfer
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
      // Verify everyone's balances before the primary transfer
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
      // Verify everyone's balances before the primary transfer
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
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, 2,
                                                                     creator_id, creator_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision);

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
wdump((prop_update_op));

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
      const asset_id_type sub_asset_1_id = create_sub_asset_and_mint(sub_asset_1_name, sub_asset_precision,
                                                                     creator_id, creator_private_key,
                                                                     req_backing_per_subdivision,
                                                                     min_price_per_subdivision);

      // Create an asset that is NOT added to the inventory
      const string wild_sub_asset_name = "SERIESA.WILD";
      const uint64_t& whole_token_subdivisions = asset::scaled_precision(sub_asset_precision).value;
      const uint64_t& max_supply = whole_token_subdivisions;
      const uint16_t flags = DEFAULT_UIA_ASSET_ISSUER_PERMISSION;
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

BOOST_AUTO_TEST_SUITE_END()
