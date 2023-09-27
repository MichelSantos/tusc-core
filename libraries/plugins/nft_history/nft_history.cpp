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

#include <graphene/nft_history/nft_history.hpp>

#include <graphene/chain/nft_object.hpp>
#include <graphene/chain/operation_history_object.hpp>

namespace graphene {
   namespace nft_history {

      namespace detail {

         class nft_history_impl {
         public:
            explicit nft_history_impl(nft_history &_plugin);

            virtual ~nft_history_impl();

            void on_block(const signed_block &b);

            graphene::chain::database &database() const {
               return _self.database();
            }

            friend class graphene::nft_history::nft_history;

            asset get_royalty_reservoir_by_token(const asset_id_type asset_id, bool throw_if_not_found = true) const;
            asset get_royalty_reservoir_by_token(const string asset_name, bool throw_if_not_found = true) const;

            vector<asset> get_royalty_reservoir_by_series(const asset_id_type series_asset_id,
                                                          bool throw_if_not_found = true) const;
            vector<asset> get_royalty_reservoir_by_series(const string series_name,
                                                          bool throw_if_not_found = true) const;

            vector <asset> get_royalties_by_series(const asset_id_type series_asset_id,
                                                   const fc::time_point_sec start,
                                                   const fc::time_point_sec end) const;

            vector <asset> get_royalties_by_series(const string series_name,
                                                   const fc::time_point_sec start,
                                                   const fc::time_point_sec end) const;

            asset get_royalties_collected_by_token(const asset_id_type token_id,
                                                   const fc::time_point_sec start,
                                                   const fc::time_point_sec end) const;

            asset get_royalties_collected_by_token(const string token_name,
                                                   const fc::time_point_sec start,
                                                   const fc::time_point_sec end) const;

            const asset_id_type get_parent_series(const graphene::chain::database& db,
                                                  const asset_id_type& nft_asset_id) const;
         private:
            nft_history &_self;

            std::string _plugin_option = "";

            const asset_object* get_asset_from_string(const graphene::chain::database& db,
                                                      const std::string& symbol_or_id,
                                                      bool throw_if_not_found) const;
         };

         struct operation_process_nft_related {
            nft_history &_plugin;
            fc::time_point_sec _now;
            graphene::chain::database& d;
            nft_history_impl &_impl;

            operation_process_nft_related( nft_history& nhp, graphene::chain::database& db, fc::time_point_sec n, nft_history_impl& history_impl)
               :_plugin(nhp),_now(n),d(db),_impl(history_impl) {

            }

            typedef void result_type;

            /** do nothing for other operation types */
            template<typename T>
            void operator()( const T& )const{}

            void operator()( const nft_royalty_paid_operation& op ) const {
               // Check whether the transaction asset is an NFT asset
               const asset_id_type &token_id = op.tx_amount.asset_id;

               const auto& token_id_idx = d.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
               auto token_itr = token_id_idx.find(token_id);
               const bool& is_nft = (token_itr != token_id_idx.end());
               if (!is_nft) {
                  return;
               }

               const fc::time_point_sec block_time = _now;
               const asset_id_type series_id = _impl.get_parent_series(d, token_id);
               d.create<nft_royalty_object>( [block_time, series_id, token_id, &op]( nft_royalty_object& ro ) {
                  ro.timestamp = block_time;
                  ro.series_asset_id = series_id;
                  ro.token_asset_id = token_id;
                  ro.royalty = op.royalty;
               });
            }
         };

         void nft_history_impl::on_block(const signed_block &b) {
            graphene::chain::database& db = database();
            const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
            for( const optional< operation_history_object >& o_op : hist )
            {
               if( o_op.valid() ) {
                  // Process NFT-related operations
                  try{
                     o_op->op.visit( operation_process_nft_related( _self, db, b.timestamp, *this) );
                  } FC_CAPTURE_AND_LOG( (o_op) )
               }
            }
         }

         nft_history_impl::nft_history_impl(nft_history &_plugin) :
            _self(_plugin) {
            // Initialize implementation as necessary
         }

         nft_history_impl::~nft_history_impl() {
            // Shut-down implementation as necessary
         }

         const asset_object* nft_history_impl::get_asset_from_string(const graphene::chain::database &db,
                                                                     const std::string &symbol_or_id,
                                                                     bool throw_if_not_found) const {
            // TODO: Cache the result to avoid repeatedly fetching from db
            FC_ASSERT(symbol_or_id.size() > 0);
            const asset_object *asset = nullptr;
            if (std::isdigit(symbol_or_id[0])) {
               asset = db.find(fc::variant(symbol_or_id, 1).as<asset_id_type>(1));
            } else {
               const auto &idx = db.get_index_type<asset_index>().indices().get<by_symbol>();
               auto itr = idx.find(symbol_or_id);
               if (itr != idx.end())
                  asset = &*itr;
            }

            if (throw_if_not_found) {
               FC_ASSERT(asset, "No such asset");
            }

            return asset;
         }

         const asset_id_type nft_history_impl::get_parent_series(const graphene::chain::database& db,
                                                                 const asset_id_type& nft_asset_id) const {
            // TODO: Cache the result to avoid repeatedly fetching from db
            const auto &token_id_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
            auto token_idx_itr = token_id_idx.find(nft_asset_id);
            const bool is_found = token_idx_itr != token_id_idx.end();

            if (!is_found) {
               FC_THROW("No such asset");
            }

            return token_idx_itr->series_id;
         }

         asset nft_history_impl::get_royalty_reservoir_by_token(const asset_id_type asset_id, bool throw_if_not_found) const {
            graphene::chain::database& db = database();
            const auto &token_id_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_asset_id>();
            auto token_idx_itr = token_id_idx.find(asset_id);
            const bool is_found = token_idx_itr != token_id_idx.end();
            if ( !is_found) {
               if (throw_if_not_found) {
                  FC_ASSERT(false, "No such asset");
               } else {
                  return asset(0);
               }
            }

            return token_idx_itr->royalty_reservoir;
         }

         asset nft_history_impl::get_royalty_reservoir_by_token(const string asset_name, bool throw_if_not_found) const {
            graphene::chain::database& db = database();
            const auto &token_id_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_name>();
            auto token_idx_itr = token_id_idx.find(asset_name);
            const bool is_found = token_idx_itr != token_id_idx.end();
            if ( !is_found) {
               if (throw_if_not_found) {
                  FC_ASSERT(false, "No such asset");
               } else {
                  return asset(0);
               }
            }

            return token_idx_itr->royalty_reservoir;
         }

         vector<asset> nft_history_impl::get_royalty_reservoir_by_series(const string series_name, bool throw_if_not_found) const {
            graphene::chain::database &db = database();
            const asset_id_type series_asset_id = get_asset_from_string(db, series_name, throw_if_not_found)->id;

            return get_royalty_reservoir_by_series(series_asset_id, throw_if_not_found);
         }

         vector<asset> nft_history_impl::get_royalty_reservoir_by_series(const asset_id_type series_asset_id, bool throw_if_not_found) const {
            graphene::chain::database& db = database();

            const auto &series_id_idx = db.get_index_type<nft_token_index>().indices().get<by_nft_token_series_id>();

            nft_token_id_type start_id = nft_token_id_type();
            // Construct an lower bound to lower possible entry for the series
            // (a) the Asset ID of the specified series
            // (b) the Token ID
            auto lower_itr = series_id_idx.lower_bound(boost::make_tuple(series_asset_id, start_id));

            if ( throw_if_not_found && (lower_itr == series_id_idx.end()) ) {
               FC_THROW("No such NFT series");
            }

            // Construct an upper bound at the maximum value possible for the combination of
            // (a) the Asset ID of the specified series
            // (b) the Token ID
            // to limit search results to only the specified Series
            auto upper_itr = series_id_idx.upper_bound(boost::make_tuple(series_asset_id, nft_token_id_type(GRAPHENE_DB_MAX_INSTANCE_ID)));

            // Loop through every token in the Series
            std::map<asset_id_type, share_type> mapAssetIDToAsset;
            for ( ; lower_itr != upper_itr && lower_itr != series_id_idx.end(); ++lower_itr) {
               const asset& royalty_for_token = lower_itr->royalty_reservoir;

               if (mapAssetIDToAsset.find(royalty_for_token.asset_id) != mapAssetIDToAsset.end()) {
                  mapAssetIDToAsset[royalty_for_token.asset_id] += royalty_for_token.amount;
               } else {
                  mapAssetIDToAsset[royalty_for_token.asset_id] = royalty_for_token.amount;
               }
            }

            // Organize output
            vector<asset> royalties;
            std::map<asset_id_type, share_type>::iterator it = mapAssetIDToAsset.begin();
            while (it != mapAssetIDToAsset.end()) {
               asset a(it->second, it->first);
               royalties.emplace_back(a);
               it++;
            }

            return royalties;
         }

         vector <asset> nft_history_impl::get_royalties_by_series(const string series_name,
                                                                  const fc::time_point_sec start,
                                                                  const fc::time_point_sec end) const {
            graphene::chain::database &db = database();
            const asset_id_type series_asset_id = get_asset_from_string(db, series_name, true)->id;

            return get_royalties_by_series(series_asset_id, start, end);
         }

         vector <asset> nft_history_impl::get_royalties_by_series(const asset_id_type series_asset_id,
                                                                  const fc::time_point_sec start,
                                                                  const fc::time_point_sec end) const {
            graphene::chain::database &db = database();

            const auto& royalty_idx = db.get_index_type<nft_royalty_index>();
            const auto& royalty_time_idx = royalty_idx.indices().get<by_nft_series_timestamp>();

            // Loop through timespan
            std::map<asset_id_type, share_type> mapAssetIDToAsset;
            auto itr = royalty_time_idx.lower_bound(std::make_tuple(series_asset_id, start));
            while( itr != royalty_time_idx.end() && itr->series_asset_id == series_asset_id && itr->timestamp < end) {
               const asset& royalty = itr->royalty;
               if (mapAssetIDToAsset.find(royalty.asset_id) != mapAssetIDToAsset.end()) {
                  mapAssetIDToAsset[royalty.asset_id] += royalty.amount;
               } else {
                  mapAssetIDToAsset[royalty.asset_id] = royalty.amount;
               }

               ++itr;
            }

            // Organize output
            vector<asset> royalties;
            std::map<asset_id_type, share_type>::iterator it = mapAssetIDToAsset.begin();
            while (it != mapAssetIDToAsset.end()) {
               asset a(it->second, it->first);
               royalties.emplace_back(a);
               it++;
            }

            return royalties;
         }

         asset nft_history_impl::get_royalties_collected_by_token(const string token_name,
                                                                  const fc::time_point_sec start,
                                                                  const fc::time_point_sec end) const {
            graphene::chain::database &db = database();
            const asset_id_type asset_id = get_asset_from_string(db, token_name, true)->id;

            return get_royalties_collected_by_token(asset_id, start, end);
         }

         asset nft_history_impl::get_royalties_collected_by_token(const asset_id_type asset_id,
                                                                  const fc::time_point_sec start,
                                                                  const fc::time_point_sec end) const {
            graphene::chain::database &db = database();

            const auto& royalty_idx = db.get_index_type<nft_royalty_index>();
            const auto& royalty_time_idx = royalty_idx.indices().get<by_nft_token_timestamp>();

            // Loop through timespan
            bool is_royalty_type_identified = false;
            asset_id_type royalty_type;
            share_type cumulative;
            auto itr = royalty_time_idx.lower_bound(std::make_tuple(asset_id, start));
            while( itr != royalty_time_idx.end() && itr->token_asset_id == asset_id && itr->timestamp < end) {
               if (!is_royalty_type_identified) {
                  royalty_type = itr->royalty.asset_id;
                  is_royalty_type_identified = true;
               }
               FC_ASSERT(itr->royalty.asset_id == royalty_type,
                         "The type of royalty collected does not match the expected type");

               cumulative += itr->royalty.amount;

               ++itr;
            }

            return asset(cumulative, royalty_type);
         }

      } // end namespace detail

      nft_history::nft_history(graphene::app::application &app) :
         plugin(app),
         my(std::make_unique<detail::nft_history_impl>(*this)) {
         // Add needed code here
      }

      nft_history::~nft_history() {
         cleanup();
      }

      std::string nft_history::plugin_name() const {
         return "nft_history";
      }

      std::string nft_history::plugin_description() const {
         return "nft_history description";
      }

      void nft_history::plugin_set_program_options(
         boost::program_options::options_description &cli,
         boost::program_options::options_description &cfg
      ) {
         cli.add_options()
            ("nft_history_option", boost::program_options::value<std::string>(), "nft_history option");
         cfg.add(cli);
      }

      void nft_history::plugin_initialize(const boost::program_options::variables_map &options) {
         database().applied_block.connect([&](const signed_block &b) {
            my->on_block(b);
         });

         database().add_index< primary_index< nft_royalty_index > >();

         if (options.count("nft_history") > 0) {
            my->_plugin_option = options["nft_history"].as<std::string>();
         }
      }

      void nft_history::plugin_startup() {
         ilog("nft_history: plugin_startup() begin");
      }

      void nft_history::plugin_shutdown() {
         ilog("nft_history: plugin_shutdown() begin");
         cleanup();
      }

      void nft_history::cleanup() {
         // Add cleanup code here
      }

      asset nft_history::get_royalty_reservoir_by_token(const asset_id_type asset_id, bool throw_if_not_found) const {
         return my->get_royalty_reservoir_by_token(asset_id, throw_if_not_found);
      }

      asset nft_history::get_royalty_reservoir_by_token(const string asset_name, bool throw_if_not_found) const {
         return my->get_royalty_reservoir_by_token(asset_name, throw_if_not_found);
      }

      vector<asset> nft_history::get_royalty_reservoir_by_series(const asset_id_type series_asset_id, bool throw_if_not_found) const {
         return my->get_royalty_reservoir_by_series(series_asset_id, throw_if_not_found);
      }

      vector<asset> nft_history::get_royalty_reservoir_by_series(const string series_name, bool throw_if_not_found) const {
         return my->get_royalty_reservoir_by_series(series_name, throw_if_not_found);
      }

      vector <asset> nft_history::get_royalties_by_series(const asset_id_type series_asset_id,
                                                          const fc::time_point_sec start,
                                                          const fc::time_point_sec end) const {
         return my->get_royalties_by_series(series_asset_id, start, end);
      }

      vector <asset> nft_history::get_royalties_by_series(const string series_name,
                                                          const fc::time_point_sec start,
                                                          const fc::time_point_sec end) const {
         return my->get_royalties_by_series(series_name, start, end);
      }

      asset nft_history::get_royalties_collected_by_token(const asset_id_type token_id,
                                                          const fc::time_point_sec start,
                                                          const fc::time_point_sec end) const {
         return my->get_royalties_collected_by_token(token_id, start, end);
      }

      asset nft_history::get_royalties_collected_by_token(const string token_name,
                                                          const fc::time_point_sec start,
                                                          const fc::time_point_sec end) const {
         return my->get_royalties_collected_by_token(token_name, start, end);
      }
   }
}