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

#include <fc/time.hpp>
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/memo.hpp>

namespace graphene {
   namespace protocol {
      struct nft_series_create_operation : public base_operation {
         struct fee_parameters_type {
            uint64_t fee = 5000 * GRAPHENE_BLOCKCHAIN_PRECISION;
         };

         /// Paid to the network
         asset fee;

         /// This account must sign and pay the fee for this operation.
         /// This account must also be the associated asset's issuer.
         account_id_type issuer;

         /// Existing asset that will be "upgraded" to a series
         asset_id_type asset_id;

         /// Royalty fee percentage (RFP) for secondary sales and transfers.
         /// This percentage will be paid to the designated Beneficiary.
         /// This is a fixed point value, representing hundredths of a percent, i.e. a value of 100
         /// in this field means a 1% fee is charged on market trades of this asset.
         uint16_t royalty_fee_centipercent = 0;

         /// Beneficiary of royalty payments from secondary sales and secondary transfers
         account_id_type beneficiary;

         /// Series Manager
         optional<account_id_type> manager;

         /// for future expansion
         extensions_type extensions;

         /***
          * @brief Perform simple validation of this object
          */
         void validate() const;

         /**
          * @brief Who will pay the fee
          */
         account_id_type fee_payer() const { return issuer; }

      };

      struct nft_mint_operation : public base_operation {
         struct fee_parameters_type {
            uint64_t fee = 10 * GRAPHENE_BLOCKCHAIN_PRECISION;
         };

         /// Paid to the network
         asset fee;

         /// This account must sign and pay the fee for this operation
         /// This account must also be the Series Issuer.
         account_id_type issuer;

         /// Existing asset type that will be "minted" into the inventory
         asset_id_type asset_id;

         /// Subdivision to mint
         share_type subdivisions;

         /// Minimum price per subdivision
         asset min_price_per_subdivision;

         /// Required backing per subdivision
         asset req_backing_per_subdivision;

         /// for future expansion
         extensions_type extensions;

         /***
          * @brief Perform simple validation of this object
          */
         void validate() const;

         /**
          * @brief Who will pay the fee
          */
         account_id_type fee_payer() const { return issuer; }

      };

      struct nft_primary_transfer_operation : public base_operation {
         struct fee_parameters_type {
            uint64_t fee       = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
            uint32_t price_per_kbyte = 10 * GRAPHENE_BLOCKCHAIN_PRECISION; /// only required for large memos.
         };

         /// Paid to the network
         asset fee;

         /// The amount of an asset to transfer from an Series Inventory associated with the asset to @ref to
         asset amount;

         /// Account to transfer asset to
         account_id_type to;

         /// User provided data encrypted to the memo key of the "to" account
         optional<memo_data> memo;

         /// This account must sign and pay the fee for this operation
         /// This account must be the Manager of the Series containing the minted asset.
         account_id_type manager;

         /// Account that will provision the backing that may be required by the NFT
         optional<account_id_type> provisioner;

         /// for future expansion
         extensions_type extensions;

         /***
          * @brief Perform simple validation of this object
          */
         void validate() const;
         share_type calculate_fee(const fee_parameters_type& k) const;

         /**
          * @brief Who will pay the fee
          */
         account_id_type fee_payer() const { return manager; }
      };

      struct nft_return_operation : public base_operation {
         struct fee_parameters_type {
            uint64_t fee = 10 * GRAPHENE_BLOCKCHAIN_PRECISION;
         };

         /// Paid to the network
         asset fee;

         /// The amount of an asset to transfer back into the associated Series Inventory.
         /// The asset_id embedded within the amount determines which Series Inventory is associated.
         asset amount;

         /// Bearer of this amount
         account_id_type bearer;

         /// for future expansion
         extensions_type extensions;

         /***
          * @brief Perform simple validation of this object
          */
         void validate() const;

         /**
          * @brief Who will pay the fee
          */
         account_id_type fee_payer() const { return bearer; }

      };
   }
}

FC_REFLECT( graphene::protocol::nft_series_create_operation::fee_parameters_type, (fee)
)

FC_REFLECT( graphene::protocol::nft_series_create_operation,
(fee)(issuer)(asset_id)(royalty_fee_centipercent)(beneficiary)(manager)(extensions)
)

FC_REFLECT( graphene::protocol::nft_mint_operation::fee_parameters_type, (fee)
)

FC_REFLECT( graphene::protocol::nft_mint_operation,
(fee)(issuer)(asset_id)(subdivisions)(min_price_per_subdivision)(req_backing_per_subdivision)(extensions)
)

FC_REFLECT( graphene::protocol::nft_primary_transfer_operation::fee_parameters_type, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::protocol::nft_primary_transfer_operation,
(fee)(amount)(to)(memo)(manager)(provisioner)(extensions)
)

FC_REFLECT( graphene::protocol::nft_return_operation::fee_parameters_type, (fee)
)

FC_REFLECT( graphene::protocol::nft_return_operation,
(fee)(bearer)(amount)(extensions)
)

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::nft_series_create_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::nft_series_create_operation )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::nft_mint_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::nft_mint_operation )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::nft_primary_transfer_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::nft_primary_transfer_operation )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::nft_return_operation::fee_parameters_type )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::nft_return_operation )
