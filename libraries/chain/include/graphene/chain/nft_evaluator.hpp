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

#include <graphene/chain/evaluator.hpp>
#include <graphene/protocol/nft.hpp>

namespace graphene {
   namespace chain {

      class nft_series_create_evaluator : public evaluator<nft_series_create_evaluator> {
      public:
         typedef nft_series_create_operation operation_type;

         void_result do_evaluate(const nft_series_create_operation &o);

         object_id_type do_apply(const nft_series_create_operation &o);

         const asset_object* asset_to_associate = nullptr;
      };

      class nft_mint_evaluator : public evaluator<nft_mint_evaluator> {
      public:
         typedef nft_mint_operation operation_type;

         void_result do_evaluate(const nft_mint_operation &o);

         object_id_type do_apply(const nft_mint_operation &o);

         const asset_object* token_to_associate = nullptr;
         const asset_object* series_to_associate = nullptr;
         const asset_dynamic_data_object* token_dyn_data = nullptr;
      };

      class nft_primary_transfer_evaluator : public evaluator<nft_primary_transfer_evaluator> {
      public:
         typedef nft_primary_transfer_operation operation_type;

         void_result do_evaluate(const nft_primary_transfer_operation &o);

         void_result do_apply(const nft_primary_transfer_operation &o);

         const nft_token_object* ptr_token_obj = nullptr;
         bool _is_backing_required = false;
         asset _required_backing;
      };

      class nft_return_evaluator : public evaluator<nft_return_evaluator> {
      public:
         typedef nft_return_operation operation_type;

         void_result do_evaluate(const nft_return_operation &o);

         void_result do_apply(const nft_return_operation &o);

         const nft_token_object* _ptr_token_obj = nullptr;
         asset _redemption_amount;
      };

   } // namespace graphene
} // namespace graphene
