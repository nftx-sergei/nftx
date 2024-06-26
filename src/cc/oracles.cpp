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

#include "komodo_defs.h"
#include "CCOracles.h"
#include <secp256k1.h>

/*
 An oracles CC has the purpose of converting offchain data into onchain data
 simplest would just be to have a pubkey(s) that are trusted to provide such data, but this wont need to have a CC involved at all and can just be done by convention
 
 That begs the question, "what would an oracles CC do?"
 A couple of things come to mind, ie. payments to oracles for future offchain data and maybe some sort of dispute/censoring ability
 
 first step is to define the data that the oracle is providing. A simple name:description tx can be created to define the name and description of the oracle data.
 linked to this txid would be two types of transactions:
    a) oracle providers
    b) oracle data users
 
 In order to be resistant to sybil attacks, the feedback mechanism needs to have a cost. combining with the idea of payments for data, the oracle providers will be ranked by actual payments made to each oracle for each data type.
 
 Implementation notes:
  In order to maintain good performance even under heavy usage, special marker utxo are used. Actually a pair of them. When a provider registers to be a data provider, a special unspendable normal output is created to allow for quick scanning. Since the marker is based on the oracletxid, it becomes a single address where all the providers can be found. 
 
  A convention is used so that the datafee can be changed by registering again. it is assumed that there wont be too many of these datafee changes. if more than one from the same provider happens in the same block, the lower price is used.
 
  The other efficiency issue is finding the most recent data point. We want to create a linked list of all data points, going back to the first one. In order to make this efficient, a special and unique per provider/oracletxid baton utxo is used. This should have exactly one utxo, so the search would be a direct lookup and it is passed on from one data point to the next. There is some small chance that the baton utxo is spent in a non-data transaction, so provision is made to allow for recreating a baton utxo in case it isnt found. The baton utxo is a convenience and doesnt affect validation
 
 Required transactions:
 0) create oracle description -> just needs to create txid for oracle data
 1) register as oracle data provider with price -> become a registered oracle data provider
 2) pay provider for N oracle data points -> lock funds for oracle provider
 3) publish oracle data point -> publish data and collect payment
 
 The format string is a set of chars with the following meaning:
  's' -> <256 char string
  'S' -> <65536 char string
  'd' -> <256 binary data
  'D' -> <65536 binary data
  'c' -> 1 byte signed little endian number, 'C' unsigned
  't' -> 2 byte signed little endian number, 'T' unsigned
  'i' -> 4 byte signed little endian number, 'I' unsigned
  'l' -> 8 byte signed little endian number, 'L' unsigned
  'h' -> 32 byte hash
 
 create:
 vins.*: normal inputs
 vout.0: txfee tag to oracle normal address
 vout.1: change, if any
 vout.n-1: opreturn with name and description and format for data
 
 register:
 vins.*: normal inputs
 vout.0: txfee tag to normal marker address
 vout.1: baton CC utxo
 vout.2: change, if any
 vout.n-1: opreturn with oracletxid, pubkey and price per data point
 
 subscribe:
 vins.*: normal inputs
 vout.0: subscription fee to publishers CC address
 vout.1: change, if any
 vout.n-1: opreturn with oracletxid, registered provider's pubkey, amount
 
 data:
 vin.0: normal input
 vin.1: baton CC utxo (most of the time)
 vin.2+: subscription or data vout.0
 vout.0: change to publishers CC address
 vout.1: baton CC utxo
 vout.2: payment for dataprovider
 vout.3: change, if any
 vout.n-1: opreturn with oracletxid, prevbatontxid and data in proper format
 
 data (without payment) this is not needed as publisher can pay themselves!
 vin.0: normal input
 vin.1: baton CC utxo
 vout.0: txfee to publishers normal address
 vout.1: baton CC utxo
 vout.2: change, if any
 vout.n-1: opreturn with oracletxid, prevbatontxid and data in proper format

*/
#define PUBKEY_SPOOFING_FIX_ACTIVATION 1563148800
#define CC_MARKER_VALUE 10000
#define CC_TXFEE 10000

// start of consensus code
CScript EncodeOraclesCreateOpRet(uint8_t funcid, std::string name, std::string description, std::string format)
{
    CScript opret;
    uint8_t evalcode = EVAL_ORACLES;
    uint8_t version = 1;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << name << format << description);
    return opret;
}

uint8_t DecodeOraclesCreateOpRet(const CScript& scriptPubKey, std::string& name, std::string& description, std::string& format)
{
    std::vector<uint8_t> vopret;
    uint8_t e, f, funcid, version;
    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() > 2 && vopret[0] == EVAL_ORACLES) {
        if (vopret[1] == 'C') {
            if (E_UNMARSHAL(vopret, ss >> e; ss >> f; ss >> version; ss >> name; ss >> format; ss >> description) != 0) {
                return vopret[1];
            } else
                fprintf(stderr, "DecodeOraclesCreateOpRet unmarshal error for C\n");
        }
    }
    return 0;
}

CScript EncodeOraclesOpRet(uint8_t funcid, uint256 oracletxid, CPubKey pk, int64_t num)
{
    CScript opret;
    uint8_t evalcode = EVAL_ORACLES;
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << oracletxid << pk << num);
    return opret;
}

uint8_t DecodeOraclesOpRet(const CScript& scriptPubKey, uint256& oracletxid, CPubKey& pk, int64_t& num)
{
    std::vector<uint8_t> vopret;
    uint8_t e, f, version;

    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() > 2 && vopret[0] == EVAL_ORACLES) {
        if (vopret[0] == EVAL_ORACLES && (vopret[1] == 'R' || vopret[1] == 'S' || vopret[1] == 'F') && E_UNMARSHAL(vopret, ss >> e; ss >> f; ss >> version; ss >> oracletxid; ss >> pk; ss >> num) != 0)
            return f;
        else
            return vopret[1];
    }
    return 0;
}

CScript EncodeOraclesData(uint8_t funcid, uint256 oracletxid, uint256 batontxid, CPubKey pk, std::vector<uint8_t> data)
{
    CScript opret;
    uint8_t evalcode = EVAL_ORACLES;
    uint8_t version = 1;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << oracletxid << batontxid << pk << data);
    return (opret);
}

uint8_t DecodeOraclesData(const CScript& scriptPubKey, uint256& oracletxid, uint256& batontxid, CPubKey& pk, std::vector<uint8_t>& data)
{
    std::vector<uint8_t> vopret;
    uint8_t e, f, version;
    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> e; ss >> f; ss >> version; ss >> oracletxid; ss >> batontxid; ss >> pk; ss >> data) != 0) {
        if (e == EVAL_ORACLES && f == 'D')
            return (f);
        //else fprintf(stderr,"DecodeOraclesData evalcode.%d f.%c\n",e,f);
    } //else fprintf(stderr,"DecodeOraclesData not enough opereturn data\n");
    return (0);
}

CPubKey OracleBatonPk(char* batonaddr, struct CCcontract_info* cp)
{
    static secp256k1_context* ctx;
    size_t clen = CPubKey::PUBLIC_KEY_SIZE;
    secp256k1_pubkey pubkey;
    CPubKey batonpk;
    uint8_t priv[32];
    if (ctx == 0)
        ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    Myprivkey(priv);
    cp->unspendableEvalcode2 = EVAL_ORACLES;
    for (int32_t i = 0; i < 32; i++)
        cp->unspendablepriv2[i] = (priv[i] ^ cp->CCpriv[i]);
    while (secp256k1_ec_seckey_verify(ctx, cp->unspendablepriv2) == 0) {
        // for (i=0; i<32; i++)
        //     fprintf(stderr,"%02x",cp->unspendablepriv2[i]);
        fprintf(stderr, " invalid privkey\n");
        if (secp256k1_ec_privkey_tweak_add(ctx, cp->unspendablepriv2, priv) != 0)
            break;
    }
    if (secp256k1_ec_pubkey_create(ctx, &pubkey, cp->unspendablepriv2) != 0) {
        secp256k1_ec_pubkey_serialize(ctx, (unsigned char*)batonpk.begin(), &clen, &pubkey, SECP256K1_EC_COMPRESSED);
        cp->unspendablepk2 = batonpk;
        Getscriptaddress(batonaddr, MakeCC1vout(cp->evalcode, 0, batonpk).scriptPubKey);
        //fprintf(stderr,"batonpk.(%s) -> %s\n",(char *)HexStr(batonpk).c_str(),batonaddr);
        strcpy(cp->unspendableaddr2, batonaddr);
    } else
        fprintf(stderr, "error creating pubkey\n");
    memset(priv, 0, sizeof(priv));
    return (batonpk);
}

int64_t OracleCurrentDatafee(uint256 reforacletxid, char* markeraddr, CPubKey publisher)
{
    uint256 txid, oracletxid, hashBlock;
    int64_t datafee = 0, dfee;
    int32_t dheight = 0, vout, height;
    CTransaction tx;
    CPubKey pk;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_ORACLES);
    SetCCunspents(unspentOutputs, markeraddr, false);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        height = (int32_t)it->second.blockHeight;
        // if ( (GetLatestTimestamp(komodo_currentheight())<JUNE2021_NNELECTION_HARDFORK ? myGetTransaction(txid,tx,hashBlock)!=0 : FetchCCtx(txid,tx,cp)) && (numvouts = tx.vout.size()) > 0 )
        if (FetchCCtx(txid, tx, cp) && tx.vout.size() > 0) // version1 is true for tokel
        {
            if (DecodeOraclesOpRet(tx.vout.back().scriptPubKey, oracletxid, pk, dfee) == 'R') {
                if (oracletxid == reforacletxid && pk == publisher) {
                    if (height > dheight || (height == dheight && dfee < datafee)) {
                        dheight = height;
                        datafee = dfee;
                        if (0 && dheight != 0)
                            fprintf(stderr, "set datafee %.8f height.%d\n", (double)datafee / COIN, height);
                    }
                }
            }
        }
    }
    return (datafee);
}

int64_t OracleDatafee(CScript& scriptPubKey, uint256 oracletxid, CPubKey publisher)
{
    CTransaction oracletx;
    char markeraddr[64];
    uint256 hashBlock;
    std::string name, description, format;
    int64_t datafee = 0;
    if (myGetTransaction(oracletxid, oracletx, hashBlock) && oracletx.vout.size() > 0) {
        if (DecodeOraclesCreateOpRet(oracletx.vout.back().scriptPubKey, name, description, format) == 'C') {
            CCtxidaddr(markeraddr, oracletxid);
            datafee = OracleCurrentDatafee(oracletxid, markeraddr, publisher);
        } else {
            fprintf(stderr, "Could not decode op_ret from transaction %s\nscriptPubKey: %s\n", oracletxid.GetHex().c_str(), oracletx.vout.back().scriptPubKey.ToString().c_str());
        }
    }
    return (datafee);
}

static uint256 myIs_baton_spentinmempool(uint256 batontxid, int32_t batonvout)
{
    std::vector<CTransaction> tmp_txs;

    myGet_mempool_txs(tmp_txs, EVAL_ORACLES, 'D');
    for (std::vector<CTransaction>::const_iterator it = tmp_txs.begin(); it != tmp_txs.end(); it++) {
        const CTransaction& tx = *it;
        if (tx.vout.size() > 0 && tx.vin.size() > 1 && batontxid == tx.vin[1].prevout.hash && batonvout == tx.vin[1].prevout.n) {
            const uint256& txid = tx.GetHash();
            //char str[65]; fprintf(stderr,"found baton spent in mempool %s\n",uint256_str(str,txid));
            return (txid);
        }
    }
    return (batontxid);
}

uint256 OracleBatonUtxo(uint64_t value, struct CCcontract_info* cp, uint256 reforacletxid, char* batonaddr, CPubKey publisher, std::vector<uint8_t>& dataarg)
{
    uint256 txid, oracletxid, hashBlock, btxid, batontxid = zeroid;
    int64_t dfee;
    int32_t dheight = 0, vout, height;
    CTransaction tx;
    CPubKey pk;
    std::vector<uint8_t> vopret, data;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;

    SetCCunspents(unspentOutputs, batonaddr, true);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        height = (int32_t)it->second.blockHeight;
        if (it->second.satoshis != value) {
            //fprintf(stderr,"it->second.satoshis %llu != %llu txfee\n",(long long)it->second.satoshis,(long long)value);
            continue;
        }
        if (FetchCCtx(txid, tx, cp) && tx.vout.size() > 0) {
            GetOpReturnData(tx.vout.back().scriptPubKey, vopret);
            if (vopret.size() > 2) {
                if ((vopret[1] == 'D' && DecodeOraclesData(tx.vout.back().scriptPubKey, oracletxid, btxid, pk, data) == 'D') || (vopret[1] == 'R' && DecodeOraclesOpRet(tx.vout.back().scriptPubKey, oracletxid, pk, dfee) == 'R')) {
                    if (oracletxid == reforacletxid && pk == publisher) {
                        if (height > dheight) {
                            dheight = height;
                            batontxid = txid;
                            if (vopret[1] == 'D')
                                dataarg = data;
                            //char str[65]; fprintf(stderr,"set batontxid %s height.%d\n",uint256_str(str,batontxid),height);
                        }
                    }
                }
            }
        }
    }
    while (myIsutxo_spentinmempool(ignoretxid, ignorevin, batontxid, 1) != 0)
        batontxid = myIs_baton_spentinmempool(batontxid, 1);
    return (batontxid);
}

uint256 OraclesBatontxid(uint256 reforacletxid, CPubKey refpk)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    CTransaction regtx;
    uint256 hash, txid, batontxid, oracletxid;
    CPubKey pk;
    int32_t height, maxheight = 0;
    int64_t datafee;
    char markeraddr[64], batonaddr[64];
    std::vector<uint8_t> data;
    struct CCcontract_info *cp, C;
    batontxid = zeroid;
    cp = CCinit(&C, EVAL_ORACLES);
    CCtxidaddr(markeraddr, reforacletxid);
    SetCCunspents(unspentOutputs, markeraddr, false);
    //char str[67]; fprintf(stderr,"markeraddr.(%s) %s\n",markeraddr,pubkey33_str(str,(uint8_t *)&refpk));
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        txid = it->first.txhash;
        //fprintf(stderr,"check %s\n",uint256_str(str,txid));
        height = (int32_t)it->second.blockHeight;
        if (FetchCCtx(txid, regtx, cp)) {
            if (regtx.vout.size() >= 2 && DecodeOraclesOpRet(regtx.vout.back().scriptPubKey, oracletxid, pk, datafee) == 'R' && oracletxid == reforacletxid && pk == refpk) {
                Getscriptaddress(batonaddr, regtx.vout[1].scriptPubKey);
                batontxid = OracleBatonUtxo(CC_MARKER_VALUE, cp, oracletxid, batonaddr, pk, data);
                break;
            }
        }
    }
    return (batontxid);
}


/*int64_t OraclePrice(int32_t height,uint256 reforacletxid,char *markeraddr,char *format)
{
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    CTransaction regtx; uint256 hash,txid,oracletxid,batontxid; CPubKey pk; int32_t i,ht,maxheight=0; int64_t datafee,price; char batonaddr[64]; std::vector <uint8_t> data; struct CCcontract_info *cp,C; std::vector <struct oracleprice_info> publishers; std::vector <int64_t> prices;
    if ( format[0] != 'L' )
        return(0);
    cp = CCinit(&C,EVAL_ORACLES);
    SetCCunspents(unspentOutputs,markeraddr,false);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        ht = (int32_t)it->second.blockHeight;
        if ( myGetTransaction(txid,regtx,hash) != 0 )
        {
            if ( regtx.vout.size() > 0 && DecodeOraclesOpRet(regtx.vout[regtx.vout.size()-1].scriptPubKey,oracletxid,pk,datafee) == 'R' && oracletxid == reforacletxid )
            {
                Getscriptaddress(batonaddr,regtx.vout[1].scriptPubKey);
                batontxid = OracleBatonUtxo(cp,oracletxid,batonaddr,pk,data);
                if ( batontxid != zeroid && (ht= oracleprice_add(publishers,pk,ht,data,maxheight)) > maxheight )
                    maxheight = ht;
            }
        }
    }
    if ( maxheight > 10 )
    {
        for (std::vector<struct oracleprice_info>::const_iterator it=publishers.begin(); it!=publishers.end(); it++)
        {
            if ( it->height >= maxheight-10 )
            {
                oracle_format(&hash,&price,0,'L',(uint8_t *)it->data.data(),0,(int32_t)it->data.size());
                if ( price != 0 )
                    prices.push_back(price);
            }
        }
        return(OracleCorrelatedPrice(height,prices));
    }
    return(0);
}*/

int64_t IsOraclesvout(struct CCcontract_info* cp, const CTransaction& tx, int32_t v)
{
    //char destaddr[64];
    if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0) {
        //if ( Getscriptaddress(destaddr,tx.vout[v].scriptPubKey) > 0 && strcmp(destaddr,cp->unspendableCCaddr) == 0 )
        return (tx.vout[v].nValue);
    }
    return (0);
}

bool OraclesDataValidate(struct CCcontract_info* cp, Eval* eval, const CTransaction& tx, uint256 oracletxid, CPubKey publisher, int64_t datafee)
{
    static uint256 zerohash;
    CTransaction vinTx;
    uint256 hashBlock, activehash;
    int64_t inputs = 0, outputs = 0, assetoshis;
    CScript scriptPubKey;
    if (OracleDatafee(scriptPubKey, oracletxid, publisher) != datafee)
        return eval->Invalid("mismatched datafee");
    scriptPubKey = MakeCC1vout(cp->evalcode, 0, publisher).scriptPubKey;
    for (int32_t i = 0; i < tx.vin.size(); i++) {
        //fprintf(stderr,"vini.%d\n",i);
        if ((*cp->ismyvin)(tx.vin[i].scriptSig) != 0) {
            if (i == 0)
                return eval->Invalid("unexpected vin.0 is CC");
            //fprintf(stderr,"vini.%d check mempool\n",i);
            else if (eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock) == 0)
                return eval->Invalid("cant find vinTx");
            else {
                //fprintf(stderr,"vini.%d check hash and vout\n",i);
                //if ( hashBlock == zerohash )
                //    return eval->Invalid("cant Oracles from mempool");
                if ((assetoshis = IsOraclesvout(cp, vinTx, tx.vin[i].prevout.n)) != 0) {
                    if (i == 1 && vinTx.vout[1].scriptPubKey != tx.vout[1].scriptPubKey)
                        return eval->Invalid("baton violation");
                    else if (i != 1 && scriptPubKey == vinTx.vout[tx.vin[i].prevout.n].scriptPubKey)
                        inputs += assetoshis;
                }
            }
        }
    }
    for (int32_t i = 0; i < tx.vout.size(); i++) {
        //fprintf(stderr,"i.%d of numvouts.%d\n",i,numvouts);
        if ((assetoshis = IsOraclesvout(cp, tx, i)) != 0) {
            if (i < 2) {
                if (i == 0) {
                    if (tx.vout[0].scriptPubKey == scriptPubKey)
                        outputs += assetoshis;
                    else
                        return eval->Invalid("invalid CC vout CC destination");
                }
            }
        }
    }
    if (inputs != outputs + datafee) {
        fprintf(stderr, "inputs %llu vs outputs %llu + datafee %llu\n", (long long)inputs, (long long)outputs, (long long)datafee);
        return eval->Invalid("mismatched inputs != outputs + datafee");
    } else
        return true;
}

/*nt32_t GetLatestTimestamp(int32_t height)
{
    if ( KOMODO_NSPV_SUPERLITE ) return (NSPV_blocktime(height));
    return(komodo_heightstamp(height));
} */

bool OraclesValidate(struct CCcontract_info* cp, Eval* eval, const CTransaction& tx, uint32_t nIn)
{
    uint256 oracletxid, batontxid, txid;
    int32_t preventCCvins, preventCCvouts;
    int64_t amount;
    uint256 hashblock;
    std::vector<uint8_t> vopret;
    std::vector<uint8_t> data;

    CPubKey publisher, tmppk, oraclespk;
    char tmpaddress[64], vinaddress[64], oraclesaddr[64];
    CTransaction tmptx;
    std::string name, desc, format;

    preventCCvins = preventCCvouts = -1;
    if (tx.vout.size() < 1)
        return eval->Invalid("no vouts");
    else {
        //if (GetLatestTimestamp(komodo_currentheight())>=JUNE2021_NNELECTION_HARDFORK)
        if (true) // always true for tokel
        {
            CCOpretCheck(eval, tx, true, true, true);
            ExactAmounts(eval, tx, CC_TXFEE);
        }
        GetOpReturnData(tx.vout.back().scriptPubKey, vopret);
        if (vopret.size() > 2) {
            oraclespk = GetUnspendable(cp, 0);
            Getscriptaddress(oraclesaddr, CScript() << ParseHex(HexStr(oraclespk)) << OP_CHECKSIG);
            switch (vopret[1]) {
            case 'C': // create
                // vins.*: normal inputs
                // vout.0: marker to oracle normal address
                // vout.1: change, if any
                // vout.n-1: opreturn with name and description and format for data
                return eval->Invalid("unexpected OraclesValidate for create");
            case 'F': // fund (activation on Jul 15th 2019 00:00)
                // vins.*: normal inputs
                // vout.0: CC marker fee to oracle CC address of users pubkey
                // vout.1: change, if any
                // vout.n-1: opreturn with createtxid, pubkey and amount
                return eval->Invalid("unexpected OraclesValidate for create");
            case 'R': // register
                // vin.0: normal inputs
                // vin.1: CC input from pubkeys oracle CC addres - to prove that register came from pubkey that is registred (activation on Jul 15th 2019 00:00)
                // vout.0: marker to oracle normal address
                // vout.1: baton CC utxo
                // vout.2: marker from oraclesfund tx to normal pubkey address (activation on Jul 15th 2019 00:00)
                // vout.n-2: change, if any
                // vout.n-1: opreturn with createtxid, pubkey and price per data point
                if (GetLatestTimestamp(eval->GetCurrentHeightCompat()) > PUBKEY_SPOOFING_FIX_ACTIVATION) {
                    if (tx.vout.size() < 1 || DecodeOraclesOpRet(tx.vout.back().scriptPubKey, oracletxid, tmppk, amount) != 'R')
                        return eval->Invalid("invalid oraclesregister OP_RETURN data!");
                    else if (!eval->GetTxUnconfirmed(oracletxid, tmptx, hashblock))
                        return eval->Invalid("invalid oraclescreate txid!");
                    else if (tmptx.vout.size() < 1 || DecodeOraclesCreateOpRet(tmptx.vout.back().scriptPubKey, name, desc, format) != 'C')
                        return eval->Invalid("invalid oraclescreate OP_RETURN data!");
                    else if (IsCCInput(tmptx.vin[0].scriptSig) != 0)
                        return eval->Invalid("vin.0 is normal for oraclescreate!");
                    else if (ConstrainVout(tmptx.vout[0], 0, oraclesaddr, CC_MARKER_VALUE) == false)
                        return eval->Invalid("invalid marker for oraclescreate!");
                    else if (tx.vin.size() < 2)
                        return eval->Invalid("vin number too low");
                    else if (IsCCInput(tx.vin[0].scriptSig) != 0)
                        return eval->Invalid("vin.0 is normal for oraclesregister!");
                    else if ((*cp->ismyvin)(tx.vin[1].scriptSig) == 0 || myGetTransaction(tx.vin[1].prevout.hash, tmptx, hashblock) == 0 || DecodeOraclesOpRet(tmptx.vout[tmptx.vout.size() - 1].scriptPubKey, txid, tmppk, amount) != 'F' || !GetCCaddress(cp, tmpaddress, tmppk) || ConstrainVout(tmptx.vout[tx.vin[1].prevout.n], 1, tmpaddress, CC_MARKER_VALUE) == 0 || oracletxid != txid)
                        return eval->Invalid("invalid vin.1 for oraclesregister, it must be CC vin or pubkey not same as vin pubkey, register and fund tx must be done from owner of pubkey that registers to oracle!!");
                    else if (CCtxidaddr(tmpaddress, oracletxid).IsValid() && ConstrainVout(tx.vout[0], 0, tmpaddress, CC_MARKER_VALUE) == 0)
                        return eval->Invalid("invalid marker for oraclesregister!");
                    else if (!Getscriptaddress(tmpaddress, CScript() << ParseHex(HexStr(tmppk)) << OP_CHECKSIG) || ConstrainVout(tx.vout[2], 0, tmpaddress, CC_MARKER_VALUE) == 0)
                        return eval->Invalid("pubkey in OP_RETURN and in vout.2 not matching, register must be done from owner of pubkey that registers to oracle!");
                } else
                    return eval->Invalid("unexpected OraclesValidate for register");
                break;
            case 'S': // subscribe
                // vins.*: normal inputs
                // vout.0: subscription fee to publishers CC address
                // vout.1: change, if any
                // vout.n-1: opreturn with createtxid, registered provider's pubkey, amount
                return eval->Invalid("unexpected OraclesValidate for subscribe");
            case 'D': // data
                // vin.0: normal input
                // vin.1: baton CC utxo (most of the time)
                // vin.2+: subscription vout.0
                // vout.0: change to publishers CC address
                // vout.1: baton CC utxo
                // vout.2: payment for dataprovider
                // vout.3: change, if any
                if (tx.vin.size() >= 2 && tx.vout.size() >= 3 && DecodeOraclesData(tx.vout.back().scriptPubKey, oracletxid, batontxid, publisher, data) == 'D') {
                    if (OraclesDataValidate(cp, eval, tx, oracletxid, publisher, tx.vout[2].nValue) != 0) {
                        return true;
                    } else
                        return false;
                }
                return eval->Invalid("unexpected OraclesValidate 'D' tx invalid");
            default:
                fprintf(stderr, "illegal oracles funcid.(%c)\n", vopret[1]);
                return eval->Invalid("unexpected OraclesValidate funcid");
            }
        } else
            return eval->Invalid("unexpected oracles missing funcid");
        return PreventCC(eval, tx, preventCCvins, tx.vin.size(), preventCCvouts, tx.vout.size());
    }
    return true;
}
// end of consensus code

// helper functions for rpc calls in rpcwallet.cpp

int64_t AddOracleInputs(struct CCcontract_info* cp, CMutableTransaction& mtx, uint256 oracletxid, CPubKey pk, int64_t total, int32_t maxinputs)
{
    char coinaddr[64], funcid;
    int64_t nValue, price, totalinputs = 0;
    uint256 tmporacletxid, tmpbatontxid, txid, hashBlock;
    std::vector<uint8_t> origpubkey, data;
    CTransaction oracletx;
    int32_t vout, n = 0;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    CPubKey tmppk;
    int64_t tmpnum;

    GetCCaddress(cp, coinaddr, pk);
    SetCCunspents(unspentOutputs, coinaddr, true);
    //fprintf(stderr,"addoracleinputs from (%s)\n",coinaddr);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        //char str[65]; fprintf(stderr,"oracle check %s/v%d\n",uint256_str(str,txid),vout);
        if (myGetTransaction(txid, oracletx, hashBlock) && oracletx.vout.size() > 1) {
            if ((funcid = DecodeOraclesOpRet(oracletx.vout.back().scriptPubKey, tmporacletxid, tmppk, tmpnum)) != 0 && (funcid == 'S' || funcid == 'D')) {
                if (funcid == 'D' && ValidateCCtx(oracletx, cp) && DecodeOraclesData(oracletx.vout.back().scriptPubKey, tmporacletxid, tmpbatontxid, tmppk, data) == 0)
                    fprintf(stderr, "invalid oraclesdata transaction \n");
                else if (tmporacletxid == oracletxid) {
                    // get valid CC payments
                    if ((nValue = IsOraclesvout(cp, oracletx, vout)) >= CC_MARKER_VALUE && myIsutxo_spentinmempool(ignoretxid, ignorevin, txid, vout) == 0) {
                        if (total != 0 && maxinputs != 0)
                            mtx.vin.push_back(CTxIn(txid, vout, CScript()));
                        nValue = it->second.satoshis;
                        totalinputs += nValue;
                        n++;
                        if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
                            break;
                    } //else fprintf(stderr,"nValue %.8f or utxo memspent\n",(double)nValue/COIN);
                }
            }
        } else
            fprintf(stderr, "couldnt find transaction\n");
    }
    return (totalinputs);
}

int64_t LifetimeOraclesFunds(struct CCcontract_info* cp, uint256 oracletxid, CPubKey publisher)
{
    char coinaddr[64];
    CPubKey pk;
    int64_t total = 0, num;
    uint256 txid, hashBlock, subtxid;
    CTransaction subtx;
    std::vector<uint256> txids;
    GetCCaddress(cp, coinaddr, publisher);
    SetCCtxids(txids, coinaddr, true, cp->evalcode, 0, oracletxid, 'S');
    //fprintf(stderr,"scan lifetime of %s\n",coinaddr);
    for (std::vector<uint256>::const_iterator it = txids.begin(); it != txids.end(); it++) {
        txid = *it;
        if (myGetTransaction(txid, subtx, hashBlock) != 0) {
            if (subtx.vout.size() > 0 && DecodeOraclesOpRet(subtx.vout[subtx.vout.size() - 1].scriptPubKey, subtxid, pk, num) == 'S' && subtxid == oracletxid && pk == publisher) {
                total += subtx.vout[0].nValue;
            }
        }
    }
    return (total);
}

int64_t AddMyOraclesFunds(struct CCcontract_info* cp, CMutableTransaction& mtx, CPubKey pk, uint256 oracletxid)
{
    char coinaddr[64], funcid;
    int64_t nValue, tmpamount;
    uint256 tmporacletxid, txid, hashBlock, ignoretxid;
    int32_t vout, ignorevin;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    CTransaction vintx;
    CPubKey tmppk;

    GetCCaddress(cp, coinaddr, pk);
    SetCCunspents(unspentOutputs, coinaddr, true);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        nValue = it->second.satoshis;
        if (myGetTransaction(txid, vintx, hashBlock) != 0 && vintx.vout.size() > 0) {
            if ((funcid = DecodeOraclesOpRet(vintx.vout.back().scriptPubKey, tmporacletxid, tmppk, tmpamount)) != 0 && funcid == 'F' && tmppk == pk && tmporacletxid == oracletxid && tmpamount == nValue && myIsutxo_spentinmempool(ignoretxid, ignorevin, txid, vout) == 0) {
                mtx.vin.push_back(CTxIn(txid, vout, CScript()));
                return (nValue);
            }
        } else
            fprintf(stderr, "couldnt find transaction\n");
    }

    std::vector<CTransaction> tmp_txs;
    myGet_mempool_txs(tmp_txs, EVAL_ORACLES, 'F');
    for (std::vector<CTransaction>::const_iterator it = tmp_txs.begin(); it != tmp_txs.end(); it++) {
        const CTransaction& txmempool = *it;
        const uint256& hash = txmempool.GetHash();
        nValue = txmempool.vout[0].nValue;

        if ((funcid = DecodeOraclesOpRet(txmempool.vout[txmempool.vout.size() - 1].scriptPubKey, tmporacletxid, tmppk, tmpamount)) != 0 && funcid == 'F' && tmppk == pk && tmporacletxid == oracletxid && tmpamount == nValue && myIsutxo_spentinmempool(ignoretxid, ignorevin, hash, 0) == 0) {
            mtx.vin.push_back(CTxIn(hash, 0, CScript()));
            return (nValue);
        }
    }
    return (0);
}

UniValue OracleCreate(const CPubKey& pk, int64_t txfee, std::string name, std::string description, std::string format)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk, Oraclespk;
    struct CCcontract_info *cp, C;
    char fmt;

    cp = CCinit(&C, EVAL_ORACLES);
    if (name.size() > 32)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "name." << (int32_t)name.size() << " must be less then 32");
    if (description.size() > 4096)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "description." << (int32_t)description.size() << " must be less then 4096");
    if (format.size() > 4096)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "format." << (int32_t)format.size() << " must be less then 4096");
    if (name.size() == 0)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "name must not be empty");
    for (int i = 0; i < format.size(); i++) {
        fmt = format[i];
        switch (fmt) {
        case 's':
        case 'S':
        case 'd':
        case 'D':
        case 'c':
        case 'C':
        case 't':
        case 'T':
        case 'i':
        case 'I':
        case 'l':
        case 'L':
        case 'h':
            break;
        default:
            CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "invalid format type");
        }
    }
    if (txfee == 0)
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLES] ? 0 : CC_TXFEE;
    mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());
    Oraclespk = GetUnspendable(cp, 0);
    if (AddNormalinputs(mtx, mypk, txfee + CC_MARKER_VALUE, 0x1000, pk.IsValid()) > 0) {
        mtx.vout.push_back(CTxOut(CC_MARKER_VALUE, CScript() << ParseHex(HexStr(Oraclespk)) << OP_CHECKSIG));
        return (FinalizeCCTxExt(pk.IsValid(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cp, mtx, mypk, txfee, EncodeOraclesCreateOpRet('C', name, description, format)));
    }
    CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "error adding normal inputs");
}

UniValue OracleFund(const CPubKey& pk, int64_t txfee, uint256 oracletxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction tx;
    CPubKey mypk, oraclespk;
    struct CCcontract_info *cp, C;
    std::string name, desc, format;
    uint256 hashBlock;

    if (GetLatestTimestamp(komodo_currentheight()) < PUBKEY_SPOOFING_FIX_ACTIVATION)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "oraclesfund not active yet, activation scheduled for July 15th");
    if (!myGetTransaction(oracletxid, tx, hashBlock) || tx.vout.size() <= 0)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    if (DecodeOraclesCreateOpRet(tx.vout.back().scriptPubKey, name, desc, format) != 'C')
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    cp = CCinit(&C, EVAL_ORACLES);
    if (txfee == 0)
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLES] ? 0 : CC_TXFEE;
    mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());
    if (AddNormalinputs(mtx, mypk, txfee + CC_MARKER_VALUE, 0x1000, pk.IsValid())) {
        mtx.vout.push_back(MakeCC1vout(cp->evalcode, CC_MARKER_VALUE, mypk));
        return (FinalizeCCTxExt(pk.IsValid(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cp, mtx, mypk, txfee, EncodeOraclesOpRet('F', oracletxid, mypk, CC_MARKER_VALUE)));
    }
    CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "error adding normal inputs");
}

UniValue OracleRegister(const CPubKey& pk, int64_t txfee, uint256 oracletxid, int64_t datafee)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CPubKey mypk, markerpubkey, batonpk, oraclespk;
    struct CCcontract_info *cp, C;
    char markeraddr[64], batonaddr[64];
    std::string name, desc, format;
    uint256 hashBlock;
    CTransaction tx;

    cp = CCinit(&C, EVAL_ORACLES);
    if (txfee == 0)
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLES] ? 0 : CC_TXFEE;
    if (!myGetTransaction(oracletxid, tx, hashBlock) || tx.vout.size() <= 0)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    if (DecodeOraclesCreateOpRet(tx.vout.back().scriptPubKey, name, desc, format) != 'C')
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    if (datafee < txfee)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "datafee must be txfee or more");
    mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());
    oraclespk = GetUnspendable(cp, 0);
    batonpk = OracleBatonPk(batonaddr, cp);
    markerpubkey = CCtxidaddr(markeraddr, oracletxid);
    if (AddNormalinputs(mtx, mypk, txfee + 2 * CC_MARKER_VALUE, 0x1000, pk.IsValid())) {
        if (GetLatestTimestamp(komodo_currentheight()) > PUBKEY_SPOOFING_FIX_ACTIVATION && AddMyOraclesFunds(cp, mtx, mypk, oracletxid) != CC_MARKER_VALUE)
            CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "error adding inputs from your Oracles CC address, please fund it first with oraclesfund rpc!");
        mtx.vout.push_back(CTxOut(CC_MARKER_VALUE, CScript() << ParseHex(HexStr(markerpubkey)) << OP_CHECKSIG));
        mtx.vout.push_back(MakeCC1vout(cp->evalcode, CC_MARKER_VALUE, batonpk));
        if (GetLatestTimestamp(komodo_get_current_height()) > PUBKEY_SPOOFING_FIX_ACTIVATION)
            mtx.vout.push_back(CTxOut(CC_MARKER_VALUE, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
        return (FinalizeCCTxExt(pk.IsValid(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cp, mtx, mypk, txfee, EncodeOraclesOpRet('R', oracletxid, mypk, datafee)));
    }
    CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "error adding normal inputs");
}

UniValue OracleSubscribe(const CPubKey& pk, int64_t txfee, uint256 oracletxid, CPubKey publisher, int64_t amount)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction tx;
    CPubKey mypk, markerpubkey;
    struct CCcontract_info *cp, C;
    char markeraddr[64];
    std::string name, desc, format;
    uint256 hashBlock;
    cp = CCinit(&C, EVAL_ORACLES);
    if (txfee == 0)
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLES] ? 0 : CC_TXFEE;
    if (!myGetTransaction(oracletxid, tx, hashBlock) || tx.vout.size() <= 0)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    if (DecodeOraclesCreateOpRet(tx.vout.back().scriptPubKey, name, desc, format) != 'C')
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());
    markerpubkey = CCtxidaddr(markeraddr, oracletxid);
    if (AddNormalinputs(mtx, mypk, amount + txfee + CC_MARKER_VALUE, 0x1000, pk.IsValid()) > 0) {
        mtx.vout.push_back(MakeCC1vout(cp->evalcode, amount, publisher));
        mtx.vout.push_back(CTxOut(CC_MARKER_VALUE, CScript() << ParseHex(HexStr(markerpubkey)) << OP_CHECKSIG));
        return (FinalizeCCTxExt(pk.IsValid(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cp, mtx, mypk, txfee, EncodeOraclesOpRet('S', oracletxid, mypk, amount)));
    }
    CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "error adding normal inputs");
}

UniValue OracleData(const CPubKey& pk, int64_t txfee, uint256 oracletxid, std::vector<uint8_t> data)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CScript pubKey;
    CPubKey mypk, batonpk;
    int64_t offset, datafee, inputs, CCchange = 0;
    struct CCcontract_info *cp, C;
    uint256 batontxid, hashBlock;
    char coinaddr[64], batonaddr[64];
    std::vector<uint8_t> prevdata;
    CTransaction tx;
    std::string name, description, format;
    int32_t len;

    cp = CCinit(&C, EVAL_ORACLES);
    mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());
    if (data.size() > 8192)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "datasize " << (int32_t)data.size() << " is too big");
    if ((datafee = OracleDatafee(pubKey, oracletxid, mypk)) <= 0)
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "datafee " << (double)datafee / COIN << "is illegal");
    if (myGetTransaction(oracletxid, tx, hashBlock) && tx.vout.size() > 0) {
        if (DecodeOraclesCreateOpRet(tx.vout.back().scriptPubKey, name, description, format) == 'C') {
            if (oracle_parse_data_format(data, format) == 0)
                CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "data does not match length or content format specification");
        } else
            CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "invalid oracle txid opret data");
    } else
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "invalid oracle txid");
    if (txfee == 0)
        txfee = ASSETCHAINS_CCZEROTXFEE[EVAL_ORACLES] ? 0 : CC_TXFEE;
    GetCCaddress(cp, coinaddr, mypk);
    if (AddNormalinputs(mtx, mypk, txfee + CC_MARKER_VALUE, 0x1000, pk.IsValid()) > 0) // have enough funds even if baton utxo not there
    {
        batonpk = OracleBatonPk(batonaddr, cp);
        batontxid = OracleBatonUtxo(CC_MARKER_VALUE, cp, oracletxid, batonaddr, mypk, prevdata);
        if (batontxid != zeroid) // not impossible to fail, but hopefully a very rare event
            mtx.vin.push_back(CTxIn(batontxid, 1, CScript()));
        else
            fprintf(stderr, "warning: couldnt find baton utxo %s\n", batonaddr);
        if ((inputs = AddOracleInputs(cp, mtx, oracletxid, mypk, datafee, 0x1000)) >= datafee) {
            if (inputs > datafee)
                CCchange = (inputs - datafee);
            mtx.vout.push_back(MakeCC1vout(cp->evalcode, CCchange, mypk));
            mtx.vout.push_back(MakeCC1vout(cp->evalcode, CC_MARKER_VALUE, batonpk));
            mtx.vout.push_back(CTxOut(datafee, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
            return FinalizeCCTxExt(pk.IsValid(), FINALIZECCTX_NO_CHANGE_WHEN_DUST, cp, mtx, mypk, txfee, EncodeOraclesData('D', oracletxid, batontxid, mypk, data));
        } else
            CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "couldnt find enough oracle inputs " << coinaddr << ", limit 1 per utxo");
    }
    CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "couldnt add normal inputs");
}

UniValue OracleDataSample(uint256 reforacletxid, uint256 txid)
{
    UniValue result(UniValue::VOBJ);
    CTransaction datatx, oracletx;
    uint256 hashBlock, btxid, oracletxid;
    std::string error;
    struct CCcontract_info *cp, C;
    CPubKey pk;
    std::string name, description, format;
    std::vector<uint8_t> data;
    char str[67], *formatstr = 0;

    cp = CCinit(&C, EVAL_ORACLES);
    result.push_back(Pair("result", "success"));
    if (myGetTransaction(reforacletxid, oracletx, hashBlock) != 0 && oracletx.vout.size() > 0) {
        if (DecodeOraclesCreateOpRet(oracletx.vout.back().scriptPubKey, name, description, format) == 'C') {
            if (FetchCCtx(txid, datatx, cp) && datatx.vout.size() > 0) {
                if (DecodeOraclesData(datatx.vout.back().scriptPubKey, oracletxid, btxid, pk, data) == 'D' && reforacletxid == oracletxid) {
                    if ((formatstr = (char*)format.c_str()) == 0)
                        formatstr = (char*)"";
                    result.push_back(Pair("txid", uint256_str(str, txid)));
                    result.push_back(Pair("data", OracleFormat((uint8_t*)data.data(), (int32_t)data.size(), formatstr, (int32_t)format.size())));
                    return (result);
                } else
                    error = "invalid data tx";
            } else
                error = "cannot find data txid";
        } else
            error = "invalid oracles txid";
    } else
        error = "cannot find oracles txid";
    result.push_back(Pair("result", "error"));
    result.push_back(Pair("error", error));
    return (result);
}

UniValue OracleDataSamples(uint256 reforacletxid, char* batonaddr, int32_t num)
{
    UniValue result(UniValue::VOBJ), b(UniValue::VARR);
    CTransaction tx, oracletx;
    uint256 txid, hashBlock, btxid, oracletxid;
    CPubKey pk;
    std::string name, description, format;
    int32_t n = 0, vout;
    std::vector<uint8_t> data;
    char *formatstr = 0, addr[64];
    std::vector<uint256> txids;
    int64_t nValue;
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_ORACLES);
    result.push_back(Pair("result", "success"));
    if (myGetTransaction(reforacletxid, oracletx, hashBlock) != 0 && oracletx.vout.size() > 0) {
        if (DecodeOraclesCreateOpRet(oracletx.vout.back().scriptPubKey, name, description, format) == 'C') {
            std::vector<CTransaction> tmp_txs;
            myGet_mempool_txs(tmp_txs, EVAL_ORACLES, 'D');
            for (std::vector<CTransaction>::const_iterator it = tmp_txs.begin(); it != tmp_txs.end(); it++) {
                const CTransaction& txmempool = *it;
                const uint256& hash = txmempool.GetHash();
                if (txmempool.vout.size() >= 2 && ValidateCCtx(txmempool, cp) && txmempool.vout[1].nValue == CC_MARKER_VALUE && DecodeOraclesData(txmempool.vout.back().scriptPubKey, oracletxid, btxid, pk, data) == 'D' && reforacletxid == oracletxid) {
                    Getscriptaddress(addr, txmempool.vout[1].scriptPubKey);
                    if (strcmp(addr, batonaddr) != 0)
                        continue;
                    if ((formatstr = (char*)format.c_str()) == 0)
                        formatstr = (char*)"";
                    UniValue a(UniValue::VOBJ);
                    a.push_back(Pair("txid", hash.GetHex()));
                    a.push_back(Pair("data", OracleFormat((uint8_t*)data.data(), (int32_t)data.size(), formatstr, (int32_t)format.size())));
                    b.push_back(a);
                    if (++n >= num && num != 0) {
                        result.push_back(Pair("samples", b));
                        return (result);
                    }
                }
            }
            SetCCtxids(txids, batonaddr, true, EVAL_ORACLES, CC_MARKER_VALUE, reforacletxid, 'D');
            if (txids.size() > 0) {
                for (std::vector<uint256>::const_iterator it = txids.end() - 1; it != txids.begin(); it--) {
                    txid = *it;
                    if (FetchCCtx(txid, tx, cp) && tx.vout.size() >= 2) {
                        if (tx.vout[1].nValue == CC_MARKER_VALUE && DecodeOraclesData(tx.vout.back().scriptPubKey, oracletxid, btxid, pk, data) == 'D' && reforacletxid == oracletxid) {
                            if ((formatstr = (char*)format.c_str()) == NULL)
                                formatstr = (char*)"";
                            UniValue a(UniValue::VOBJ);
                            a.push_back(Pair("txid", txid.GetHex()));
                            a.push_back(Pair("data", OracleFormat((uint8_t*)data.data(), (int32_t)data.size(), formatstr, (int32_t)format.size())));
                            b.push_back(a);
                            if (++n >= num && num != 0) {
                                result.push_back(Pair("samples", b));
                                return (result);
                            }
                        }
                    }
                }
            }
        } else
            CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    } else
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    result.push_back(Pair("samples", b));
    return (result);
}

UniValue OracleInfo(uint256 origtxid)
{
    UniValue result(UniValue::VOBJ), a(UniValue::VARR);
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;
    int32_t height;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    CTransaction tx;
    std::string name, description, format;
    uint256 hashBlock, txid, oracletxid, batontxid;
    CPubKey pk;
    struct CCcontract_info *cp, C;
    int64_t datafee, funding;
    char markeraddr[64], batonaddr[64];
    std::map<CPubKey, std::pair<uint256, int32_t>> publishers;

    cp = CCinit(&C, EVAL_ORACLES);
    CCtxidaddr(markeraddr, origtxid);
    if (!myGetTransaction(origtxid, tx, hashBlock))
        CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "cant find oracletxid " << oracletxid.GetHex());
    else {
        if (tx.vout.size() > 0 && DecodeOraclesCreateOpRet(tx.vout.back().scriptPubKey, name, description, format) == 'C') {
            result.push_back(Pair("result", "success"));
            result.push_back(Pair("txid", origtxid.GetHex()));
            result.push_back(Pair("name", name));
            result.push_back(Pair("description", description));
            result.push_back(Pair("format", format));
            result.push_back(Pair("marker", markeraddr));
            SetCCunspents(unspentOutputs, markeraddr, false);
            for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++) {
                txid = it->first.txhash;
                height = (int32_t)it->second.blockHeight;
                if (FetchCCtx(txid, tx, cp) && tx.vout.size() > 0 &&
                    DecodeOraclesOpRet(tx.vout.back().scriptPubKey, oracletxid, pk, datafee) == 'R' && oracletxid == origtxid) {
                    if (publishers.find(pk) == publishers.end() || height > publishers[pk].second) {
                        publishers[pk].first = txid;
                        publishers[pk].second = height;
                    }
                }
            }
            for (std::map<CPubKey, std::pair<uint256, int32_t>>::iterator it = publishers.begin(); it != publishers.end(); ++it) {
                if (FetchCCtx(it->second.first, tx, cp) && DecodeOraclesOpRet(tx.vout.back().scriptPubKey, oracletxid, pk, datafee) == 'R') {
                    UniValue obj(UniValue::VOBJ);
                    std::vector<uint8_t> data;

                    obj.push_back(Pair("publisher", HexStr(pk)));
                    Getscriptaddress(batonaddr, tx.vout[1].scriptPubKey);
                    batontxid = OracleBatonUtxo(CC_MARKER_VALUE, cp, oracletxid, batonaddr, pk, data);
                    obj.push_back(Pair("baton", batonaddr));
                    obj.push_back(Pair("batontxid", batontxid.GetHex()));
                    CAmount lifefunding = LifetimeOraclesFunds(cp, oracletxid, pk);
                    obj.push_back(Pair("lifetime", ValueFromAmount(lifefunding)));
                    CAmount funding = AddOracleInputs(cp, mtx, oracletxid, pk, 0, 0);
                    obj.push_back(Pair("funds", ValueFromAmount(funding)));
                    obj.push_back(Pair("datafee", ValueFromAmount(datafee)));
                    a.push_back(obj);
                }
            }
            result.push_back(Pair("registered", a));
        } else
            CCERR_RESULT("oraclescc", CCLOG_INFO, stream << "invalid oracletxid " << oracletxid.GetHex());
    }
    return (result);
}

UniValue OraclesList()
{
    UniValue result(UniValue::VARR);
    std::vector<uint256> txids;
    struct CCcontract_info *cp, C;
    uint256 txid, hashBlock;
    CTransaction createtx;
    std::string name, description, format;

    cp = CCinit(&C, EVAL_ORACLES);
    SetCCtxids(txids, cp->normaladdr, false, cp->evalcode, CC_MARKER_VALUE, zeroid, 'C');
    for (std::vector<uint256>::const_iterator it = txids.begin(); it != txids.end(); it++) {
        txid = *it;
        if (myGetTransaction(txid, createtx, hashBlock) != 0) {
            if (createtx.vout.size() > 0 && DecodeOraclesCreateOpRet(createtx.vout.back().scriptPubKey, name, description, format) == 'C') {
                result.push_back(txid.GetHex());
            }
        }
    }
    return (result);
}
