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

BOOST_AUTO_TEST_SUITE_END()
