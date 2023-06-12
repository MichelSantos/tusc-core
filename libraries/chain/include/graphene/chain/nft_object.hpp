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
#pragma once
#include <graphene/chain/types.hpp>
#include <graphene/db/generic_index.hpp>
#include <graphene/protocol/asset_ops.hpp>

#include <boost/multi_index/composite_key.hpp>

/**
 * @defgroup NFT Non-fungible token objects
 */

namespace graphene {
   namespace chain {
      class database;

      using namespace graphene::db;

      /**
       *  @brief Tracks a Series into which tokens are associated
       *  @ingroup object
       *  @ingroup implementation
       *
       *  This object exists as an implementation detail and its ID should never be referenced by
       *  a blockchain operation.
       */
      class nft_series_object : public abstract_object<nft_series_object> {
      public:
         static constexpr uint8_t space_id = implementation_ids;
         static constexpr uint8_t type_id = impl_nft_series_object_type; // Indirectly defined in graphene/chain/types.hpp

         /// Name of the series
         string series_name; // Duplicative of the symbol name from the associated asset

         /// Associated asset representation of the series
         asset_id_type asset_id;

         /// Royalty fee percentage (RFP) for secondary sales and transfers.
         /// This percentage will be paid to the royalty claimants.
         /// This is a fixed point value, representing hundredths of a percent, i.e. a value of 100
         /// in this field means a 1% fee is charged during Secondary Sales and Secondary Transfers.
         uint16_t royalty_fee_centipercent = 0;

         /// Beneficiary splits the "surplus" associated with Primary Sales
         account_id_type beneficiary;

         /// Series manager
         account_id_type manager;
      };

      struct by_nft_series_asset_id;
      struct by_nft_series_name;
      typedef multi_index_container<
         nft_series_object,
         indexed_by<
            ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
            ordered_unique< tag<by_nft_series_name>, member<nft_series_object, string, &nft_series_object::series_name> >,
            ordered_unique< tag<by_nft_series_asset_id>, member<nft_series_object, asset_id_type, &nft_series_object::asset_id> >
         >
      > nft_series_multi_index_type;
      typedef generic_index<nft_series_object, nft_series_multi_index_type> nft_series_index;


      /**
       *  @brief Tracks a token
       *  @ingroup object
       *  @ingroup implementation
       *
       *  This object exists as an implementation detail and its ID should never be referenced by
       *  a blockchain operation.
       */
      class nft_token_object : public abstract_object<nft_token_object> {
      public:
         static constexpr uint8_t space_id = implementation_ids;
         static constexpr uint8_t type_id = impl_nft_token_object_type; // Indirectly defined in graphene/chain/types.hpp

         /// Asset ID of the associated token
         asset_id_type token_id;

         /// Name of the associated token
         string token_name; // Duplicative of the symbol name from the associated asset

         /// Asset ID of the associated series
         asset_id_type series_id;

         /// Minimum price per subdivision
         asset min_price_per_subdivision;

         /// Required backing per subdivision
         asset req_backing_per_subdivision;

         /// Initial amount of token subdivisions in Series Inventory
         /// amount_in_inventory + amount_in_circulation + amount_burned should equal amount_minted
         share_type amount_minted;

         /// Current amount of token subdivisions in Series Inventory
         share_type amount_in_inventory;

         /// Amount of token that has been burned
         share_type amount_burned;

         /// Current amount of token subdivisions in Series Inventory offered for Primary Sale
         share_type amount_on_primary_sale;

         /// Current backing behind all token subdivisions
         /// Should equal amount_in_circulation * req_backing_per_subdivision in subdivisions of the req_backing_per_subdivision
         asset current_backing;
      };

      struct by_nft_token_name;
      struct by_nft_token_asset_id;
      struct by_nft_token_series_id;
      typedef multi_index_container<
         nft_token_object,
         indexed_by<
            ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
            ordered_unique< tag<by_nft_token_name>, member<nft_token_object, string, &nft_token_object::token_name> >,
            ordered_unique< tag<by_nft_token_asset_id>, member<nft_token_object, asset_id_type, &nft_token_object::token_id> >,
            ordered_unique <tag<by_nft_token_series_id>,
               composite_key<nft_token_object,
                  member<nft_token_object, asset_id_type, &nft_token_object::series_id>,
                  member<object, object_id_type, &object::id>
               >
            >
         >
      > nft_token_multi_index_type;
      typedef generic_index<nft_token_object, nft_token_multi_index_type> nft_token_index;
   }
} // graphene::chain

MAP_OBJECT_ID_TO_TYPE(graphene::chain::nft_series_object)
MAP_OBJECT_ID_TO_TYPE(graphene::chain::nft_token_object)

FC_REFLECT_DERIVED( graphene::chain::nft_series_object, (graphene::db::object),
                    (series_name)
                    (asset_id)
                    (royalty_fee_centipercent)
                    (beneficiary)
                    (manager)
                  )

FC_REFLECT_DERIVED( graphene::chain::nft_token_object, (graphene::db::object),
                    (token_id)
                    (token_name)
                    (series_id)
                    (min_price_per_subdivision)
                    (req_backing_per_subdivision)
                    (amount_minted)
                    (amount_in_inventory)
                    (amount_burned)
                    (amount_on_primary_sale)
                    (current_backing)
)

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::nft_series_object )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::nft_token_object )
