/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "key_io.h"

#include "CCinclude.h"
#include "CCassets.h"
#include "CCfaucet.h"
#include "CCrewards.h"
#include "CCdice.h"
#include "CCauction.h"
#include "CClotto.h"
#include "CCfsm.h"
#include "CCHeir.h"
#include "CCchannels.h"
#include "CCOracles.h"
#include "CCPrices.h"
#include "CCPegs.h"
#include "CCPayments.h"
#include "CCGateways.h"
#include "CCtokens.h"
#include "CCImportGateway.h"
#include "CCTokelData.h"
#include "CCagreements.h"
#include "CCtokentags.h"
#include "CCassetloans.h"


/*
 CCcustom has most of the functions that need to be extended to create a new CC contract.
 
 A CC scriptPubKey can only be spent if it is properly signed and validated. By constraining the vins and vouts, it is possible to implement a variety of functionality. CC vouts have an otherwise non-standard form, but it is properly supported by the enhanced bitcoin protocol code as a "cryptoconditions" output and the same pubkey will create a different address.
 
 This allows creation of a special address(es) for each contract type, which has the privkey public. That allows anybody to properly sign and spend it, but with the constraints on what is allowed in the validation code, the contract functionality can be implemented.
 
 what needs to be done to add a new contract:
 1. add EVAL_CODE to eval.h
 2. initialize the variables in the CCinit function below
 3. write a Validate function to reject any unsanctioned usage of vin/vout
 4. make helper functions to create rawtx for RPC functions
 5. add rpc calls to rpcserver.cpp and rpcserver.h and in one of the rpc.cpp files
 6. add the new .cpp files to src/Makefile.am
 
 IMPORTANT: make sure that all CC inputs and CC outputs are properly accounted for and reconcile to the satoshi. The built in utxo management will enforce overall vin/vout constraints but it wont know anything about the CC constraints. That is what your Validate function needs to do.
 
 Generally speaking, there will be normal coins that change into CC outputs, CC outputs that go back to being normal coins, CC outputs that are spent to new CC outputs.
 
 Make sure both the CC coins and normal coins are preserved and follow the rules that make sense. It is a good idea to define specific roles for specific vins and vouts to reduce the complexity of validation.
 */

// to create a new CCaddr, add to rpcwallet the CCaddress and start with -pubkey= with the pubkey of the new address, with its wif already imported. set normaladdr and CChexstr. run CCaddress and it will print the privkey along with autocorrect the CCaddress. which should then update the CCaddr here

// Assets
#define FUNCNAME IsAssetsInput
#define EVALCODE EVAL_ASSETS
char AssetsCChexstr[67] = { "02adf84e0e075cf90868bd4e3d34a03420e034719649c41f371fc70d8e33aa2702" };
uint8_t AssetsCCpriv[32] = { 0x9b, 0x17, 0x66, 0xe5, 0x82, 0x66, 0xac, 0xb6, 0xba, 0x43, 0x83, 0x74, 0xf7, 0x63, 0x11, 0x3b, 0xf0, 0xf3, 0x50, 0x6f, 0xd9, 0x6b, 0x67, 0x85, 0xf9, 0x7a, 0xf0, 0x54, 0x4d, 0xb1, 0x30, 0x77 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Faucet
#define FUNCNAME IsFaucetInput
#define EVALCODE EVAL_FAUCET
char FaucetCChexstr[67] = { "03682b255c40d0cde8faee381a1a50bbb89980ff24539cb8518e294d3a63cefe12" };
uint8_t FaucetCCpriv[32] = { 0xd4, 0x4f, 0xf2, 0x31, 0x71, 0x7d, 0x28, 0x02, 0x4b, 0xc7, 0xdd, 0x71, 0xa0, 0x39, 0xc4, 0xbe, 0x1a, 0xfe, 0xeb, 0xc2, 0x46, 0xda, 0x76, 0xf8, 0x07, 0x53, 0x3d, 0x96, 0xb4, 0xca, 0xa0, 0xe9 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Rewards
#define FUNCNAME IsRewardsInput
#define EVALCODE EVAL_REWARDS
char RewardsCChexstr[67] = { "03da60379d924c2c30ac290d2a86c2ead128cb7bd571f69211cb95356e2dcc5eb9" };
uint8_t RewardsCCpriv[32] = { 0x82, 0xf5, 0xd2, 0xe7, 0xd6, 0x99, 0x33, 0x77, 0xfb, 0x80, 0x00, 0x97, 0x23, 0x3d, 0x1e, 0x6f, 0x61, 0xa9, 0xb5, 0x2e, 0x5e, 0xb4, 0x96, 0x6f, 0xbc, 0xed, 0x6b, 0xe2, 0xbb, 0x7b, 0x4b, 0xb3 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Dice
#define FUNCNAME IsDiceInput
#define EVALCODE EVAL_DICE
char DiceCChexstr[67] = { "039d966927cfdadab3ee6c56da63c21f17ea753dde4b3dfd41487103e24b27e94e" };
uint8_t DiceCCpriv[32] = { 0x0e, 0xe8, 0xf5, 0xb4, 0x3d, 0x25, 0xcc, 0x35, 0xd1, 0xf1, 0x2f, 0x04, 0x5f, 0x01, 0x26, 0xb8, 0xd1, 0xac, 0x3a, 0x5a, 0xea, 0xe0, 0x25, 0xa2, 0x8f, 0x2a, 0x8e, 0x0e, 0xf9, 0x34, 0xfa, 0x77 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Lotto
#define FUNCNAME IsLottoInput
#define EVALCODE EVAL_LOTTO
char LottoCChexstr[67] = { "03f72d2c4db440df1e706502b09ca5fec73ffe954ea1883e4049e98da68690d98f" };
uint8_t LottoCCpriv[32] = { 0xb4, 0xac, 0xc2, 0xd9, 0x67, 0x34, 0xd7, 0x58, 0x80, 0x4e, 0x25, 0x55, 0xc0, 0x50, 0x66, 0x84, 0xbb, 0xa2, 0xe7, 0xc0, 0x39, 0x17, 0xb4, 0xc5, 0x07, 0xb7, 0x3f, 0xca, 0x07, 0xb0, 0x9a, 0xeb };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Finite State Machine
#define FUNCNAME IsFSMInput
#define EVALCODE EVAL_FSM
char FSMCChexstr[67] = { "039b52d294b413b07f3643c1a28c5467901a76562d8b39a785910ae0a0f3043810" };
uint8_t FSMCCpriv[32] = { 0x11, 0xe1, 0xea, 0x3e, 0xdb, 0x36, 0xf0, 0xa8, 0xc6, 0x34, 0xe1, 0x21, 0xb8, 0x02, 0xb9, 0x4b, 0x12, 0x37, 0x8f, 0xa0, 0x86, 0x23, 0x50, 0xb2, 0x5f, 0xe4, 0xe7, 0x36, 0x0f, 0xda, 0xae, 0xfc };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Auction
#define FUNCNAME IsAuctionInput
#define EVALCODE EVAL_AUCTION
char AuctionCChexstr[67] = { "037eefe050c14cb60ae65d5b2f69eaa1c9006826d729bc0957bdc3024e3ca1dbe6" };
uint8_t AuctionCCpriv[32] = { 0x8c, 0x1b, 0xb7, 0x8c, 0x02, 0xa3, 0x9d, 0x21, 0x28, 0x59, 0xf5, 0xea, 0xda, 0xec, 0x0d, 0x11, 0xcd, 0x38, 0x47, 0xac, 0x0b, 0x6f, 0x19, 0xc0, 0x24, 0x36, 0xbf, 0x1c, 0x0a, 0x06, 0x31, 0xfb };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Heir
#define FUNCNAME IsHeirInput
#define EVALCODE EVAL_HEIR
char HeirCChexstr[67] = { "03c91bef3d7cc59c3a89286833a3446b29e52a5e773f738a1ad2b09785e5f4179e" };
uint8_t HeirCCpriv[32] = { 0x9d, 0xa1, 0xf8, 0xf7, 0xba, 0x0a, 0x91, 0x36, 0x89, 0x9a, 0x86, 0x30, 0x63, 0x20, 0xd7, 0xdf, 0xaa, 0x35, 0xe3, 0x99, 0x32, 0x2b, 0x63, 0xc0, 0x66, 0x9c, 0x93, 0xc4, 0x5e, 0x9d, 0xb9, 0xce };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Channels
#define FUNCNAME IsChannelsInput
#define EVALCODE EVAL_CHANNELS
char ChannelsCChexstr[67] = { "035debdb19b1c98c615259339500511d6216a3ffbeb28ff5655a7ef5790a12ab0b" };
uint8_t ChannelsCCpriv[32] = { 0xec, 0x91, 0x36, 0x15, 0x2d, 0xd4, 0x48, 0x73, 0x22, 0x36, 0x4f, 0x6a, 0x34, 0x5c, 0x61, 0x0f, 0x01, 0xb4, 0x79, 0xe8, 0x1c, 0x2f, 0xa1, 0x1d, 0x4a, 0x0a, 0x21, 0x16, 0xea, 0x82, 0x84, 0x60 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Oracles
#define FUNCNAME IsOraclesInput
#define EVALCODE EVAL_ORACLES
//const char *OraclesCCaddr = "REt2C4ZMnX8YYX1DRpffNA4hECZTFm39e3";
//const char *OraclesNormaladdr = "RHkFKzn1csxA3fWzAsxsLWohoCgBbirXb5";
char OraclesCChexstr[67] = { "038c1d42db6a45a57eccb8981b078fb7857b9b496293fe299d2b8d120ac5b5691a" };
uint8_t OraclesCCpriv[32] = { 0xf7, 0x4b, 0x5b, 0xa2, 0x7a, 0x5e, 0x9c, 0xda, 0x89, 0xb1, 0xcb, 0xb9, 0xe6, 0x9c, 0x2c, 0x70, 0x85, 0x37, 0xdd, 0x00, 0x7a, 0x67, 0xff, 0x7c, 0x62, 0x1b, 0xe2, 0xfb, 0x04, 0x8f, 0x85, 0xbf };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Prices
#define FUNCNAME IsPricesInput
#define EVALCODE EVAL_PRICES
char PricesCChexstr[67] = { "039894cb054c0032e99e65e715b03799607aa91212a16648d391b6fa2cc52ed0cf" };
uint8_t PricesCCpriv[32] = { 0x0a, 0x3b, 0xe7, 0x5d, 0xce, 0x06, 0xed, 0xb7, 0xc0, 0xb1, 0xbe, 0xe8, 0x7b, 0x5a, 0xd4, 0x99, 0xb8, 0x8d, 0xde, 0xac, 0xb2, 0x7e, 0x7a, 0x52, 0x96, 0x15, 0xd2, 0xa0, 0xc6, 0xb9, 0x89, 0x61 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Pegs
#define FUNCNAME IsPegsInput
#define EVALCODE EVAL_PEGS
char PegsCChexstr[67] = { "03c75c1de29a35e41606363b430c08be1c2dd93cf7a468229a082cc79c7b77eece" };
uint8_t PegsCCpriv[32] = { 0x52, 0x56, 0x4c, 0x78, 0x87, 0xf7, 0xa2, 0x39, 0xb0, 0x90, 0xb7, 0xb8, 0x62, 0x80, 0x0f, 0x83, 0x18, 0x9d, 0xf4, 0xf4, 0xbd, 0x28, 0x09, 0xa9, 0x9b, 0x85, 0x54, 0x16, 0x0f, 0x3f, 0xfb, 0x65 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Marmara (reserved evalcode, the source moved to the marmara repo)
#define FUNCNAME IsMarmaraInput
#define EVALCODE EVAL_MARMARA
char MarmaraCChexstr[67] = { "03afc5be570d0ff419425cfcc580cc762ab82baad88c148f5b028d7db7bfeee61d" };
uint8_t MarmaraCCpriv[32] = { 0x7c, 0x0b, 0x54, 0x9b, 0x65, 0xd4, 0x89, 0x57, 0xdf, 0x05, 0xfe, 0xa2, 0x62, 0x41, 0xa9, 0x09, 0x0f, 0x2a, 0x6b, 0x11, 0x2c, 0xbe, 0xbd, 0x06, 0x31, 0x8d, 0xc0, 0xb9, 0x96, 0x76, 0x3f, 0x24 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Payments
#define FUNCNAME IsPaymentsInput
#define EVALCODE EVAL_PAYMENTS
char PaymentsCChexstr[67] = { "0358f1764f82c63abc7c7455555fd1d3184905e30e819e97667e247e5792b46856" };
uint8_t PaymentsCCpriv[32] = { 0x03, 0xc9, 0x73, 0xc2, 0xb8, 0x30, 0x3d, 0xbd, 0xc8, 0xd9, 0xbf, 0x02, 0x49, 0xd9, 0x65, 0x61, 0x45, 0xed, 0x9e, 0x93, 0x51, 0xab, 0x8b, 0x2e, 0xe7, 0xc7, 0x40, 0xf1, 0xc4, 0xd2, 0xc0, 0x5b };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Gateways
#define FUNCNAME IsGatewaysInput
#define EVALCODE EVAL_GATEWAYS
// wif UxJFYqEvLAjWPPRvn8NN1fRWscBxQZXZB5BBgc3HiapKVQBYNcmo
char GatewaysCChexstr[67] = { "03ea9c062b9652d8eff34879b504eda0717895d27597aaeb60347d65eed96ccb40" };
uint8_t GatewaysCCpriv[32] = { 0xf7, 0x4b, 0x5b, 0xa2, 0x7a, 0x5e, 0x9c, 0xda, 0x89, 0xb1, 0xcb, 0xb9, 0xe6, 0x9c, 0x2c, 0x70, 0x85, 0x37, 0xdd, 0x00, 0x7a, 0x67, 0xff, 0x7c, 0x62, 0x1b, 0xe2, 0xfb, 0x04, 0x8f, 0x85, 0xbf };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Tokens
#define FUNCNAME IsTokensInput
#define EVALCODE EVAL_TOKENS
char TokensCChexstr[67] = { "03e6191c70c9c9a28f9fd87089b9488d0e6c02fb629df64979c9cdb6b2b4a68d95" };
uint8_t TokensCCpriv[32] = { 0x1d, 0x0d, 0x0d, 0xce, 0x2d, 0xd2, 0xe1, 0x9d, 0xf5, 0xb6, 0x26, 0xd5, 0xad, 0xa0, 0xf0, 0x0a, 0xdd, 0x7a, 0x72, 0x7d, 0x17, 0x35, 0xb5, 0xe3, 0x2c, 0x6c, 0xa9, 0xa2, 0x03, 0x16, 0x4b, 0xcf };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Agreements
#define FUNCNAME IsAgreementsInput
#define EVALCODE EVAL_AGREEMENTS
//const char *AgreementsCCaddr = "CeoP7RGjTcfXk4zCmge1bkD9P8iyUcomdG";
//const char *AgreementsNormaladdr = "RKMpncH1SyUJojdc4PR7QYWgKFDS5Z4bxP";
char AgreementsCChexstr[67] = { "0291ca39fe72f5d0f6a65933239ca7fc608dab04eed3428ba7f425256811ddd07e" };
uint8_t AgreementsCCpriv[32] = { 0x0d, 0x88, 0x03, 0x42, 0x36, 0x25, 0xd5, 0xe3, 0x35, 0xf2, 0x84, 0x2f, 0xd8, 0x43, 0x51, 0xdf, 0x38, 0x11, 0x1f, 0x8f, 0x03, 0x5e, 0xe6, 0x39, 0xe8, 0x42, 0xf5, 0x4a, 0xbd, 0x74, 0xba, 0x97 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// AssetLoans
#define FUNCNAME IsAssetLoansInput
#define EVALCODE EVAL_ASSETLOANS
const char *AssetLoansCCaddr = "RL1XCTBvAWqhoGFCyQ78LkWzAW55wApdde";
const char *AssetLoansNormaladdr = "RHnZ1ZkHFF44X364yDs85QYEhnzGyMRF8d";
char AssetLoansCChexstr[67] = { "03644763bcfef81ac4532b59b8542abae30d32cfe4233494587547613cb2e55ff9" };
uint8_t AssetLoansCCpriv[32] = { 0x3c, 0x07, 0xcd, 0xed, 0x77, 0xce, 0x83, 0x76, 0x68, 0xeb, 0x8c, 0x6d, 0xbb, 0x3e, 0x96, 0x50, 0xe1, 0xad, 0xb7, 0x09, 0x02, 0xcc, 0x8a, 0x38, 0x88, 0x16, 0x11, 0xf8, 0x98, 0xbc, 0x9f, 0xf3 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Token Tags
#define FUNCNAME IsTokenTagsInput
#define EVALCODE EVAL_TOKENTAGS
//const char *TokenTagsCCaddr = "CMWDAw4XnCN727SSn159SSEYtwYMdqVkZU";
//const char *TokenTagsNormaladdr = "RALqE4FF2282oTzr2fRp6FzeREsxajpyU4";
char TokenTagsCChexstr[67] = { "03aaa67714884b05ab2b8d424e7065a92a5c574405b33967715b0859fecae2d74b" };
uint8_t TokenTagsCCpriv[32] = { 0xe6, 0x2a, 0xd8, 0xda, 0x4f, 0x8b, 0x28, 0x16, 0xfa, 0x51, 0x5a, 0x5e, 0x5f, 0x39, 0x37, 0xc7, 0xa3, 0x65, 0x6c, 0xda, 0xb9, 0x34, 0x34, 0xe4, 0x11, 0x45, 0xb0, 0x9d, 0xcb, 0xa9, 0x76, 0xaa };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

#define FUNCNAME IsCClibInput
#define EVALCODE EVAL_FIRSTUSER
const char *CClibNormaladdr = "RVVeUg43rNcq3mZFnvZ8yqagyzqFgUnq4u";
char CClibCChexstr[67] = { "032447d97655da079729dc024c61088ea415b22f4c15d4810ddaf2069ac6468d2f" };
uint8_t CClibCCpriv[32] = { 0x57, 0xcf, 0x49, 0x71, 0x7d, 0xb4, 0x15, 0x1b, 0x4f, 0x98, 0xc5, 0x45, 0x8d, 0x26, 0x52, 0x4b, 0x7b, 0xe9, 0xbd, 0x55, 0xd8, 0x20, 0xd6, 0xc4, 0x82, 0x0f, 0xf5, 0xec, 0x6c, 0x1c, 0xa0, 0xc0 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// ImportGateway
#define FUNCNAME IsImportGatewayInput
#define EVALCODE EVAL_IMPORTGATEWAY
char ImportGatewayCChexstr[67] = { "0397231cfe04ea32d5fafb2206773ec9fba6e15c5a4e86064468bca195f7542714" };
uint8_t ImportGatewayCCpriv[32] = { 0x65, 0xef, 0x27, 0xeb, 0x3d, 0xb0, 0xb4, 0xae, 0x0f, 0xbc, 0x77, 0xdb, 0xf8, 0x40, 0x48, 0x90, 0x52, 0x20, 0x9e, 0x45, 0x3b, 0x49, 0xd8, 0x97, 0x60, 0x8c, 0x27, 0x4c, 0x59, 0x46, 0xe1, 0xdf };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Tokens v2
#define FUNCNAME IsTokensv2Input
#define EVALCODE EVAL_TOKENSV2
char Tokensv2CChexstr[67] = { "032fd27f72591b02f13a7f9701246eb0296b2be7cfdad32c520e594844ec3d4801" };
uint8_t Tokensv2CCpriv[32] = { 0xb5, 0xba, 0x92, 0x7f, 0x53, 0x45, 0x4f, 0xf8, 0xa4, 0xad, 0x0d, 0x38, 0x30, 0x4f, 0xd0, 0x97, 0xd1, 0xb7, 0x94, 0x1b, 0x1f, 0x52, 0xbd, 0xae, 0xa2, 0xe7, 0x49, 0x06, 0x2e, 0xd2, 0x2d, 0xa5 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Assets v2
#define FUNCNAME IsAssetsv2Input
#define EVALCODE EVAL_ASSETSV2
char Assetsv2CChexstr[67] = { "0345d2e7ab018619da6ed58ccc0138c5f58a7b754bd8e9a1a9d2b811c5fe72d467" };
uint8_t Assetsv2CCpriv[32] = { 0x46, 0x58, 0x3b, 0x18, 0xee, 0x16, 0x63, 0x51, 0x6f, 0x60, 0x6e, 0x09, 0xdf, 0x9d, 0x27, 0xc8, 0xa7, 0xa2, 0x72, 0xa5, 0xd4, 0x6a, 0x9b, 0xcb, 0xd5, 0x4f, 0x7d, 0x1c, 0xb1, 0x2e, 0x63, 0x21 };
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

// Tokel TokenData validator 
#define FUNCNAME IsTokelDataInput
#define EVALCODE EVAL_TOKELDATA
#include "CCcustom.inc"
#undef FUNCNAME
#undef EVALCODE

int32_t CClib_initcp(struct CCcontract_info *cp,uint8_t evalcode)
{
    CPubKey pk; int32_t i; uint8_t pub33[33],check33[33],hash[32]; char CCaddr[64],checkaddr[64],str[67];
    cp->evalcode = evalcode;
    cp->ismyvin = IsCClibInput;
    memcpy(cp->CCpriv,CClibCCpriv,32);
    if ( evalcode == EVAL_FIRSTUSER ) // eventually make a hashchain for each evalcode
    {
        strcpy(cp->CChexstr,CClibCChexstr);
        decode_hex(pub33,33,cp->CChexstr);
        pk = buf2pk(pub33);
        Getscriptaddress(cp->normaladdr,CScript() << ParseHex(HexStr(pk)) << OP_CHECKSIG);
        if ( strcmp(cp->normaladdr,CClibNormaladdr) != 0 )
            fprintf(stderr,"CClib_initcp addr mismatch %s vs %s\n",cp->normaladdr,CClibNormaladdr);
        GetCCaddress(cp,cp->unspendableCCaddr,pk);
        if ( priv2addr(checkaddr,check33,cp->CCpriv) != 0 )
        {
            if ( buf2pk(check33) == pk && strcmp(checkaddr,cp->normaladdr) == 0 )
            {
                //fprintf(stderr,"verified evalcode.%d %s %s\n",cp->evalcode,checkaddr,pubkey33_str(str,pub33));
                return(0);
            } else fprintf(stderr,"CClib_initcp mismatched privkey -> addr %s vs %s\n",checkaddr,cp->normaladdr);
        }
    }
    else
    {
        for (i=EVAL_FIRSTUSER; i<evalcode; i++)
        {
            vcalc_sha256(0,hash,cp->CCpriv,32);
            memcpy(cp->CCpriv,hash,32);
        }
        if ( priv2addr(cp->normaladdr,pub33,cp->CCpriv) != 0 )
        {
            pk = buf2pk(pub33);
            for (i=0; i<33; i++)
                sprintf(&cp->CChexstr[i*2],"%02x",pub33[i]);
            cp->CChexstr[i*2] = 0;
            GetCCaddress(cp,cp->unspendableCCaddr,pk);
            //printf("evalcode.%d initialized\n",evalcode);
            return(0);
        }
    }
    return(-1);
}

struct CCcontract_info *CCinit(struct CCcontract_info *cp, uint8_t evalcode)
{
    // memset(cp, '\0', sizeof(*cp)); <-- it is not good to initialize objects like this. 
	// special init func now used:
    cp->clear();

    cp->evalcode = evalcode;
    bool ismixed = false;
    switch ( evalcode )
    {
        case EVAL_ASSETS:
            strcpy(cp->CChexstr,AssetsCChexstr);
            memcpy(cp->CCpriv,AssetsCCpriv,32);
            cp->validate = AssetsValidate;
            cp->ismyvin = IsAssetsInput;
            break;
        case EVAL_FAUCET:
            strcpy(cp->CChexstr,FaucetCChexstr);
            memcpy(cp->CCpriv,FaucetCCpriv,32);
            cp->validate = FaucetValidate;
            cp->ismyvin = IsFaucetInput;
            break;
        case EVAL_REWARDS:
            strcpy(cp->CChexstr,RewardsCChexstr);
            memcpy(cp->CCpriv,RewardsCCpriv,32);
            cp->validate = RewardsValidate;
            cp->ismyvin = IsRewardsInput;
            break;
        case EVAL_DICE:
            strcpy(cp->CChexstr,DiceCChexstr);
            memcpy(cp->CCpriv,DiceCCpriv,32);
            cp->validate = DiceValidate;
            cp->ismyvin = IsDiceInput;
            break;
        case EVAL_LOTTO:
            strcpy(cp->CChexstr,LottoCChexstr);
            memcpy(cp->CCpriv,LottoCCpriv,32);
            cp->validate = LottoValidate;
            cp->ismyvin = IsLottoInput;
            break;
        case EVAL_FSM:
            strcpy(cp->CChexstr,FSMCChexstr);
            memcpy(cp->CCpriv,FSMCCpriv,32);
            cp->validate = FSMValidate;
            cp->ismyvin = IsFSMInput;
            break;
        case EVAL_AUCTION:
            strcpy(cp->CChexstr,AuctionCChexstr);
            memcpy(cp->CCpriv,AuctionCCpriv,32);
            cp->validate = AuctionValidate;
            cp->ismyvin = IsAuctionInput;
            break;
        case EVAL_HEIR:
            strcpy(cp->CChexstr,HeirCChexstr);
            memcpy(cp->CCpriv,HeirCCpriv,32);
            cp->validate = HeirValidate;
            cp->ismyvin = IsHeirInput;
            break;
        case EVAL_CHANNELS:
            strcpy(cp->CChexstr,ChannelsCChexstr);
            memcpy(cp->CCpriv,ChannelsCCpriv,32);
            cp->validate = ChannelsValidate;
            cp->ismyvin = IsChannelsInput;
            break;
        case EVAL_ORACLES:
            strcpy(cp->CChexstr,OraclesCChexstr);
            memcpy(cp->CCpriv,OraclesCCpriv,32);
            cp->validate = OraclesValidate;
            cp->ismyvin = IsOraclesInput;
            break;
        case EVAL_PRICES:
            strcpy(cp->CChexstr,PricesCChexstr);
            memcpy(cp->CCpriv,PricesCCpriv,32);
            cp->validate = PricesValidate;
            cp->ismyvin = IsPricesInput;
            break;
        case EVAL_PEGS:
            strcpy(cp->CChexstr,PegsCChexstr);
            memcpy(cp->CCpriv,PegsCCpriv,32);
            cp->validate = PegsValidate;
            cp->ismyvin = IsPegsInput;
            break;
        case EVAL_PAYMENTS:
            strcpy(cp->CChexstr,PaymentsCChexstr);
            memcpy(cp->CCpriv,PaymentsCCpriv,32);
            cp->validate = PaymentsValidate;
            cp->ismyvin = IsPaymentsInput;
            break;
        case EVAL_GATEWAYS:
            strcpy(cp->CChexstr,GatewaysCChexstr);
            memcpy(cp->CCpriv,GatewaysCCpriv,32);
            cp->validate = GatewaysValidate;
            cp->ismyvin = IsGatewaysInput;
            break;

		case EVAL_TOKENS:
			strcpy(cp->CChexstr, TokensCChexstr);
			memcpy(cp->CCpriv, TokensCCpriv, 32);
			cp->validate = TokensValidate;
			cp->ismyvin = IsTokensInput;
			break;
        case EVAL_AGREEMENTS:
			//strcpy(cp->unspendableCCaddr, AgreementsCCaddr);
			//strcpy(cp->normaladdr, AgreementsNormaladdr);
			strcpy(cp->CChexstr, AgreementsCChexstr);
			memcpy(cp->CCpriv, AgreementsCCpriv, 32);
			cp->validate = AgreementsValidate;
			cp->ismyvin = IsAgreementsInput;
            ismixed = true;
			break;
		case EVAL_ASSETLOANS:
			//strcpy(cp->unspendableCCaddr, AssetLoansCCaddr);
			//strcpy(cp->normaladdr, AssetLoansNormaladdr);
			strcpy(cp->CChexstr, AssetLoansCChexstr);
			memcpy(cp->CCpriv, AssetLoansCCpriv, 32);
			cp->validate = AssetLoansValidate;
			cp->ismyvin = IsAssetLoansInput;
            ismixed = true;
			break;
        case EVAL_TOKENTAGS:
            //strcpy(cp->unspendableCCaddr, TokenTagsCCaddr);
            //strcpy(cp->normaladdr, TokenTagsNormaladdr);
            strcpy(cp->CChexstr, TokenTagsCChexstr);
            memcpy(cp->CCpriv, TokenTagsCCpriv, 32);
            cp->validate = TokenTagsValidate;
	        cp->ismyvin = IsTokenTagsInput;
            ismixed = true;
            break;
        case EVAL_IMPORTGATEWAY:
			strcpy(cp->CChexstr, ImportGatewayCChexstr);
			memcpy(cp->CCpriv, ImportGatewayCCpriv, 32);
			cp->validate = ImportGatewayValidate;
			cp->ismyvin = IsImportGatewayInput;
			break;

		case EVAL_TOKENSV2:
			strcpy(cp->CChexstr, Tokensv2CChexstr);
			memcpy(cp->CCpriv, Tokensv2CCpriv, 32);
			cp->validate = Tokensv2Validate;
			cp->ismyvin = IsTokensv2Input;
            ismixed = true;
			break;
        case EVAL_ASSETSV2:
            strcpy(cp->CChexstr,Assetsv2CChexstr);
            memcpy(cp->CCpriv,Assetsv2CCpriv,32);
            cp->validate = Assetsv2Validate;
            cp->ismyvin = IsAssetsv2Input;
            ismixed = true;
            break;

        case EVAL_TOKELDATA:
            cp->validate = TokelDataValidate;
            cp->ismyvin = IsTokelDataInput;
            ismixed = true;
            break;

        default:
            if (CClib_initcp(cp, evalcode) < 0)
                return(0);
            break;
    }
    // init addresses now here:
    if (cp->CChexstr[0])    {
        _GetCCaddress(cp->unspendableCCaddr, cp->evalcode, pubkey2pk(ParseHex(cp->CChexstr)), ismixed);
        Getscriptaddress(cp->normaladdr, CScript() << ParseHex(cp->CChexstr) << OP_CHECKSIG);
    }
    return(cp);
}

