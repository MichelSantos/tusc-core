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

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

#include <graphene/protocol/nft.hpp>

namespace graphene { namespace nft_history {
using namespace chain;

//
// Plugins should #define their SPACE_ID's so plugins with
// conflicting SPACE_ID assignments can be compiled into the
// same binary (by simply re-assigning some of the conflicting #defined
// SPACE_ID's in a build script).
//
// Assignment of SPACE_ID's cannot be done at run-time because
// various template automagic depends on them being known at compile
// time.
//
#ifndef NFT_HISTORY_SPACE_ID
#define NFT_HISTORY_SPACE_ID 8
#endif

enum nft_royalty_object_type
{
   ROYALTY_COLLECTED_TYPE_ID = 0,
   ROYALTY_DISTRIBUTED_TYPE_ID = 1
};

struct nft_royalty_collected_object : public abstract_object<nft_royalty_collected_object>
{
   static constexpr uint8_t space_id = NFT_HISTORY_SPACE_ID;
   static constexpr uint8_t type_id  = ROYALTY_COLLECTED_TYPE_ID;

   fc::time_point_sec timestamp;
   asset_id_type series_asset_id;
   asset_id_type token_asset_id;
   asset royalty;
};

struct nft_royalty_distributed_object : public abstract_object<nft_royalty_distributed_object>
{
   static constexpr uint8_t space_id = NFT_HISTORY_SPACE_ID;
   static constexpr uint8_t type_id  = ROYALTY_DISTRIBUTED_TYPE_ID;

   fc::time_point_sec timestamp;
   asset_id_type series_asset_id;
   asset royalty_distributed;
   account_id_type recipient;
};

namespace detail
{
    class nft_history_impl;
}

class nft_history : public graphene::app::plugin
{
   public:
      explicit nft_history(graphene::app::application& app);
      ~nft_history() override;

      std::string plugin_name()const override;
      std::string plugin_description()const override;
      void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      void plugin_initialize(const boost::program_options::variables_map& options) override;
      void plugin_startup() override;
      void plugin_shutdown() override;

      /**
       * @brief Get the royalty in the reservoir
       * @param token_name Token asset ID
       * @return Royalty currently in the reservoir
       */
      asset get_royalty_reservoir_by_token(const asset_id_type asset_id, bool throw_if_not_found = true) const;

      /**
       * @brief Get the royalty in the reservoir
       * @param token_name Token name
       * @return Royalty currently in the reservoir
       */
      asset get_royalty_reservoir_by_token(const string asset_name, bool throw_if_not_found = true) const;

      /**
       * @brief Get the royalty reservoir for all NFTs in a Series
       * @param series_asset_id Series asset ID
       * @return Royalty currently in the reservoirs
       */
      vector<asset> get_royalty_reservoir_by_series(const asset_id_type series_asset_id, bool throw_if_not_found = true) const;

      /**
       * @brief Get the royalty reservoir for all NFTs in a Series
       * @param series_name Series asset ID
       * @return Royalty currently in the reservoirs
       */
      vector<asset> get_royalty_reservoir_by_series(const string series_name, bool throw_if_not_found = true) const;

      /**
       * @brief Get the royalties collected for an NFT token
       * @param token_name Token asset ID
       * @param start Start time (inclusive)
       * @param end End time (exclusive)
       * @return Total royalty paid
       */
      asset get_royalties_collected_by_token(const asset_id_type token_id,
                                             const fc::time_point_sec start,
                                             const fc::time_point_sec end) const;

      /**
       * @brief Get the royalties collected for an NFT token
       * @param token_name Token name
       * @param start Start time (inclusive)
       * @param end End time (exclusive)
       * @return Total royalty paid
       */
      asset get_royalties_collected_by_token(const string token_name,
                                             const fc::time_point_sec start,
                                             const fc::time_point_sec end) const;

      /**
       * @brief Get the royalties collected for an NFT series
       * @param token_name NFT Series asset ID
       * @param start Start time (inclusive)
       * @param end End time (exclusive)
       * @return Total royalties paid
       */
      vector <asset> get_royalties_collected_by_series(const asset_id_type series_asset_id,
                                                       const fc::time_point_sec start,
                                                       const fc::time_point_sec end) const;

      /**
       * @brief Get the royalties collected for an NFT series
       * @param token_name NFT Series name
       * @param start Start time (inclusive)
       * @param end End time (exclusive)
       * @return Total royalties paid grouped by asset ID of royalty
       */
      vector <asset> get_royalties_collected_by_series(const string series_name,
                                                       const fc::time_point_sec start,
                                                       const fc::time_point_sec end) const;

      /**
       * @brief Get the royalties distributed for an NFT series
       * @param token_name NFT Series asset ID
       * @param start Start time (inclusive)
       * @param end End time (exclusive)
       * @return Total royalties distributed grouped by asset ID of royalty
       */
      vector<asset> get_royalties_distributed_by_series(const asset_id_type series_asset_id,
                                                        const fc::time_point_sec start,
                                                        const fc::time_point_sec end) const;

      /**
       * @brief Get the cumulative royalties distributed for an NFT series
       * @param token_name NFT Series name
       * @param start Start time (inclusive)
       * @param end End time (exclusive)
       * @return Total royalties distributed grouped by asset ID of royalty
       */
      vector<asset> get_royalties_distributed_by_series(const string series_name,
                                                        const fc::time_point_sec start,
                                                        const fc::time_point_sec end) const;

      /**
       * @brief Get the detailed royalties distributed for an NFT series
       * @param token_name NFT Series asset ID
       * @param start Start time (inclusive)
       * @param end End time (exclusive)
       * @return Total royalties distributed grouped by asset ID of royalty
       */
      vector<nft_royalty_distributed_object> get_royalties_distributed_details_by_series(
         const asset_id_type series_asset_id, const fc::time_point_sec start, const fc::time_point_sec end) const;

      /**
       * @brief Get the detailed royalties distributed for an NFT series
       * @param token_name NFT Series name
       * @param start Start time (inclusive)
       * @param end End time (exclusive)
       * @return Total royalties distributed grouped by asset ID of royalty
       */
      vector<nft_royalty_distributed_object> get_royalties_distributed_details_by_series(
         const string series_name, const fc::time_point_sec start, const fc::time_point_sec end) const;

   private:
      void cleanup();
      std::unique_ptr<detail::nft_history_impl> my;
};

struct by_nft_series_timestamp;
struct by_nft_token_timestamp;
typedef multi_index_container <
   nft_royalty_collected_object,
   indexed_by<
      ordered_unique < tag < by_id>, member<object, object_id_type, &object::id>>,
      ordered_non_unique <tag<by_nft_series_timestamp>,
         composite_key< nft_royalty_collected_object,
            member<nft_royalty_collected_object, asset_id_type, &nft_royalty_collected_object::series_asset_id>,
            member<nft_royalty_collected_object, fc::time_point_sec, &nft_royalty_collected_object::timestamp>
         >,
         composite_key_compare<
            std::less< asset_id_type >,
            std::less< time_point_sec >
         >
      >,
      ordered_non_unique <tag<by_nft_token_timestamp>,
         composite_key< nft_royalty_collected_object,
            member<nft_royalty_collected_object, asset_id_type, &nft_royalty_collected_object::token_asset_id>,
            member<nft_royalty_collected_object, fc::time_point_sec, &nft_royalty_collected_object::timestamp>
         >,
         composite_key_compare<
            std::less< asset_id_type >,
            std::less< time_point_sec >
         >
      >
   >
> nft_royalty_object_multi_index_type;

typedef generic_index <nft_royalty_collected_object, nft_royalty_object_multi_index_type> nft_royalty_index;

struct by_nft_recipient_timestamp;
typedef multi_index_container <
   nft_royalty_distributed_object,
   indexed_by<
      ordered_unique < tag < by_id>, member<object, object_id_type, &object::id>>,
      ordered_non_unique <tag<by_nft_series_timestamp>,
         composite_key< nft_royalty_distributed_object,
            member<nft_royalty_distributed_object, asset_id_type, &nft_royalty_distributed_object::series_asset_id>,
            member<nft_royalty_distributed_object, fc::time_point_sec, &nft_royalty_distributed_object::timestamp>
         >,
         composite_key_compare<
            std::less< asset_id_type >,
            std::less< time_point_sec >
         >
      >,
      ordered_non_unique <tag<by_nft_recipient_timestamp>,
         composite_key< nft_royalty_distributed_object,
            member<nft_royalty_distributed_object, account_id_type, &nft_royalty_distributed_object::recipient>,
            member<nft_royalty_distributed_object, fc::time_point_sec, &nft_royalty_distributed_object::timestamp>
         >,
         composite_key_compare<
            std::less< account_id_type >,
            std::less< time_point_sec >
         >
      >
   >
> nft_royalty_distributed_object_multi_index_type;

typedef generic_index <nft_royalty_distributed_object, nft_royalty_distributed_object_multi_index_type> nft_royalty_distributed_index;

} } //graphene::nft_history

FC_REFLECT_DERIVED( graphene::nft_history::nft_royalty_collected_object, (graphene::db::object), (timestamp)(series_asset_id)(token_asset_id)(royalty))
FC_REFLECT_DERIVED( graphene::nft_history::nft_royalty_distributed_object, (graphene::db::object), (timestamp)(series_asset_id)(royalty_distributed)(recipient))