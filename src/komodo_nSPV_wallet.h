
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

#ifndef KOMODO_NSPVWALLET_H
#define KOMODO_NSPVWALLET_H

// nSPV wallet uses superlite functions (and some komodod built in functions) to implement nSPV_spend
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);

int32_t NSPV_validatehdrs(struct NSPV_ntzsproofresp *ptr)
{
    int32_t i,height,txidht; CTransaction tx; uint256 blockhash,txid,desttxid;
    if ( (ptr->common.nextht-ptr->common.prevht+1) != ptr->common.numhdrs )
        return(-1);
    else if ( NSPV_txextract(tx,ptr->nextntz,ptr->nexttxlen) < 0 )
        return(-2);
    else if ( tx.GetHash() != ptr->nexttxid )
        return(-3);
    else if ( NSPV_notarizationextract(&height,&blockhash,&desttxid,tx) < 0 )
        return(-4);
    else if ( height != ptr->common.nextht )
        return(-5);
    else if ( NSPV_hdrhash(&ptr->common.hdrs[ptr->common.numhdrs-1]) != blockhash )
        return(-6);
    for (i=ptr->common.numhdrs-1; i>0; i--)
    {
        blockhash = NSPV_hdrhash(&ptr->common.hdrs[i-1]);
        if ( blockhash != ptr->common.hdrs[i].hashPrevBlock )
            return(-i-12);
    }
    if ( NSPV_txextract(tx,ptr->prevntz,ptr->prevtxlen) < 0 )
        return(-7);
    else if ( tx.GetHash() != ptr->prevtxid )
        return(-8);
    else if ( NSPV_notarizationextract(&height,&blockhash,&desttxid,tx) < 0 )
        return(-9);
    else if ( height != ptr->common.prevht )
        return(-10);
    else if ( NSPV_hdrhash(&ptr->common.hdrs[0]) != blockhash )
        return(-11);
    return(0);
}

int32_t NSPV_gettransaction(int32_t vout,uint256 txid,int32_t height,CTransaction &tx)
{
    int32_t offset,retval = 0;
    NSPV_txproof(vout,txid,height);
    if ( NSPV_txproofresult.txid != txid )
        return(-1);
    else if ( NSPV_txextract(tx,NSPV_txproofresult.tx,NSPV_txproofresult.txlen) < 0 || NSPV_txproofresult.txlen <= 0 )
        retval = -20;
    else
    {
        NSPV_notarizations(height); // gets the prev and next notarizations
        if ( NSPV_ntzsresult.prevntz.height == 0 || NSPV_ntzsresult.prevntz.height >= NSPV_ntzsresult.nextntz.height )
        {
            fprintf(stderr,"issue manual bracket\n");
            NSPV_notarizations(height-1);
            NSPV_notarizations(height+1);
            NSPV_notarizations(height); // gets the prev and next notarizations
        }
        if ( NSPV_ntzsresult.prevntz.height != 0 && NSPV_ntzsresult.prevntz.height <= NSPV_ntzsresult.nextntz.height )
        {
            //fprintf(stderr,"gettx ht.%d prev.%d next.%d\n",height,NSPV_ntzsresult.prevntz.height, NSPV_ntzsresult.nextntz.height);
            offset = (height - NSPV_ntzsresult.prevntz.height);
            if ( offset >= 0 && height <= NSPV_ntzsresult.nextntz.height )
            {
                NSPV_txidhdrsproof(NSPV_ntzsresult.prevntz.txid,NSPV_ntzsresult.nextntz.txid);
                if ( (retval= NSPV_validatehdrs(&NSPV_ntzsproofresult)) == 0 )
                {
                    std::vector<uint256> txids; std::vector<uint8_t> proof; uint256 proofroot;
                    proof.resize(NSPV_txproofresult.txprooflen);
                    memcpy(&proof[0],NSPV_txproofresult.tx,NSPV_txproofresult.txprooflen);
                    proofroot = BitcoinGetProofMerkleRoot(proof,txids);
                    if ( proofroot != NSPV_ntzsproofresult.common.hdrs[offset].hashMerkleRoot )
                    {
                        fprintf(stderr,"prooflen.%d proofroot.%s vs %s\n",NSPV_txproofresult.txprooflen,proofroot.GetHex().c_str(),NSPV_ntzsproofresult.common.hdrs[offset].hashMerkleRoot.GetHex().c_str());
                        retval = -23;
                    }
                }
            } else retval = -22;
        } else retval = -1;
    }
    fprintf(stderr,"NSPV_gettransaction retval would have been %d\n",retval);
    return(0);
}

int32_t NSPV_vinselect(int32_t *aboveip,int64_t *abovep,int32_t *belowip,int64_t *belowp,struct NSPV_utxoresp utxos[],int32_t numunspents,int64_t value)
{
    int32_t i,abovei,belowi; int64_t above,below,gap,atx_value;
    abovei = belowi = -1;
    for (above=below=i=0; i<numunspents; i++)
    {
        if ( (atx_value= utxos[i].satoshis) <= 0 )
            continue;
        if ( atx_value == value )
        {
            *aboveip = *belowip = i;
            *abovep = *belowp = 0;
            return(i);
        }
        else if ( atx_value > value )
        {
            gap = (atx_value - value);
            if ( above == 0 || gap < above )
            {
                above = gap;
                abovei = i;
            }
        }
        else
        {
            gap = (value - atx_value);
            if ( below == 0 || gap < below )
            {
                below = gap;
                belowi = i;
            }
        }
        //printf("value %.8f gap %.8f abovei.%d %.8f belowi.%d %.8f\n",dstr(value),dstr(gap),abovei,dstr(above),belowi,dstr(below));
    }
    *aboveip = abovei;
    *abovep = above;
    *belowip = belowi;
    *belowp = below;
    //printf("above.%d below.%d\n",abovei,belowi);
    if ( abovei >= 0 && belowi >= 0 )
    {
        if ( above < (below >> 1) )
            return(abovei);
        else return(belowi);
    }
    else if ( abovei >= 0 )
        return(abovei);
    else return(belowi);
}

int64_t NSPV_addinputs(struct NSPV_utxoresp *used,CMutableTransaction &mtx,int64_t total,int32_t maxinputs,struct NSPV_utxoresp *ptr,int32_t num)
{
    int32_t abovei,belowi,ind,vout,i,n = 0; int64_t threshold,above,below; int64_t remains,totalinputs = 0; CTransaction tx; struct NSPV_utxoresp utxos[NSPV_MAXVINS],*up;
    memset(utxos,0,sizeof(utxos));
    if ( maxinputs > NSPV_MAXVINS )
        maxinputs = NSPV_MAXVINS;
    if ( maxinputs > 0 )
        threshold = total/maxinputs;
    else threshold = total;
    for (i=0; i<num; i++)
    {
        if ( ptr[i].satoshis > threshold )
            utxos[n++] = ptr[i];
    }
    remains = total;
    //fprintf(stderr,"n.%d for total %.8f\n",n,(double)total/COIN);
    for (i=0; i<maxinputs && n>0; i++)
    {
        below = above = 0;
        abovei = belowi = -1;
        if ( NSPV_vinselect(&abovei,&above,&belowi,&below,utxos,n,remains) < 0 )
        {
            fprintf(stderr,"error finding unspent i.%d of %d, %.8f vs %.8f\n",i,n,(double)remains/COIN,(double)total/COIN);
            return(0);
        }
        if ( belowi < 0 || abovei >= 0 )
            ind = abovei;
        else ind = belowi;
        if ( ind < 0 )
        {
            fprintf(stderr,"error finding unspent i.%d of %d, %.8f vs %.8f, abovei.%d belowi.%d ind.%d\n",i,n,(double)remains/COIN,(double)total/COIN,abovei,belowi,ind);
            return(0);
        }
        //fprintf(stderr,"i.%d ind.%d abovei.%d belowi.%d n.%d\n",i,ind,abovei,belowi,n);
        up = &utxos[ind];
        mtx.vin.push_back(CTxIn(up->txid,up->vout,CScript()));
        used[i] = *up;
        totalinputs += up->satoshis;
        remains -= up->satoshis;
        utxos[ind] = utxos[--n];
        memset(&utxos[n],0,sizeof(utxos[n]));
        //fprintf(stderr,"totalinputs %.8f vs total %.8f i.%d vs max.%d\n",(double)totalinputs/COIN,(double)total/COIN,i,maxinputs);
        if ( totalinputs >= total || (i+1) >= maxinputs )
            break;
    }
    //fprintf(stderr,"totalinputs %.8f vs total %.8f\n",(double)totalinputs/COIN,(double)total/COIN);
    if ( totalinputs >= total )
        return(totalinputs);
    return(0);
}

bool NSPV_SignTx(CMutableTransaction &mtx,int32_t vini,int64_t utxovalue,const CScript scriptPubKey)
{
    CTransaction txNewConst(mtx); SignatureData sigdata; CBasicKeyStore keystore;
    keystore.AddKey(NSPV_key);
    if ( 0 )
    {
        int32_t i;
        for (i=0; i<scriptPubKey.size()+4; i++)
            fprintf(stderr,"%02x",((uint8_t *)&scriptPubKey)[i]);
        fprintf(stderr," scriptPubKey\n");
    }
    if ( ProduceSignature(TransactionSignatureCreator(&keystore,&txNewConst,vini,utxovalue,SIGHASH_ALL),scriptPubKey,sigdata,NSPV_BRANCHID) != 0 )
    {
        UpdateTransaction(mtx,vini,sigdata);
        return(true);
    } // else fprintf(stderr,"signing error for SignTx vini.%d %.8f\n",vini,(double)utxovalue/COIN);
    return(false);
}

std::string NSPV_signtx(CMutableTransaction &mtx,uint64_t txfee,CScript opret,struct NSPV_utxoresp used[])
{
    CTransaction vintx; std::string hex; uint256 hashBlock; int64_t interest=0,change,totaloutputs=0,totalinputs=0; int32_t i,utxovout,n;
    n = mtx.vout.size();
    for (i=0; i<n; i++)
        totaloutputs += mtx.vout[i].nValue;
    n = mtx.vin.size();
    for (i=0; i<n; i++)
    {
        totalinputs += used[i].satoshis;
        interest += used[i].extradata;
    }
    //PrecomputedTransactionData txdata(mtx);
    if ( (totalinputs+interest) >= totaloutputs+2*txfee )
    {
        change = (totalinputs+interest) - (totaloutputs+txfee);
        mtx.vout.push_back(CTxOut(change,CScript() << ParseHex(NSPV_pubkeystr) << OP_CHECKSIG));
    }
    if ( opret.size() > 0 )
        mtx.vout.push_back(CTxOut(0,opret));
    for (i=0; i<n; i++)
    {
        utxovout = mtx.vin[i].prevout.n;
        if ( NSPV_gettransaction(utxovout,mtx.vin[i].prevout.hash,used[i].height,vintx) == 0 )
        {
            if ( vintx.vout[utxovout].nValue != used[i].satoshis )
            {
                fprintf(stderr,"vintx mismatch %.8f != %.8f\n",(double)vintx.vout[utxovout].nValue/COIN,(double)used[i].satoshis/COIN);
                return("");
            }
            else if ( utxovout != used[i].vout )
            {
                fprintf(stderr,"vintx vout mismatch %d != %d\n",utxovout,used[i].vout);
                return("");
            }
            else if ( NSPV_SignTx(mtx,i,vintx.vout[utxovout].nValue,vintx.vout[utxovout].scriptPubKey) == 0 )
            {
                fprintf(stderr,"signing error for vini.%d\n",i);
                return("");
            }
        } else fprintf(stderr,"couldnt find txid.%s\n",mtx.vin[i].prevout.hash.GetHex().c_str());
    }
    fprintf(stderr,"sign %d inputs %.8f + interest %.8f -> %d outputs %.8f change %.8f\n",(int32_t)mtx.vin.size(),(double)totalinputs/COIN,(double)interest/COIN,(int32_t)mtx.vout.size(),(double)totaloutputs/COIN,(double)change/COIN);
    return(EncodeHexTx(mtx));
}

UniValue NSPV_spend(char *srcaddr,char *destaddr,int64_t satoshis) // what its all about!
{
    UniValue result(UniValue::VOBJ); uint8_t rmd160[128]; int64_t txfee = 10000;
    if ( NSPV_logintime == 0 || time(NULL) > NSPV_logintime+NSPV_AUTOLOGOUT )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","wif expired"));
        return(result);
    }
    if ( strcmp(srcaddr,NSPV_address.c_str()) != 0 )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","invalid address"));
        result.push_back(Pair("mismatched",srcaddr));
        return(result);
    }
    else if ( bitcoin_base58decode(rmd160,destaddr) != 25 )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","invalid destaddr"));
        return(result);
    }
    if ( NSPV_inforesult.height == 0 )
        NSPV_getinfo_json();
    if ( NSPV_inforesult.height == 0 )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","couldnt getinfo"));
        return(result);
    }
    if ( strcmp(NSPV_utxosresult.coinaddr,srcaddr) != 0 || NSPV_utxosresult.nodeheight < NSPV_inforesult.height )
        NSPV_addressutxos(srcaddr);
    if ( strcmp(NSPV_utxosresult.coinaddr,srcaddr) != 0 || NSPV_utxosresult.nodeheight < NSPV_inforesult.height )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("address",NSPV_utxosresult.coinaddr));
        result.push_back(Pair("srcaddr",srcaddr));
        result.push_back(Pair("nodeheight",(int64_t)NSPV_utxosresult.nodeheight));
        result.push_back(Pair("infoheight",(int64_t)NSPV_inforesult.height));
        result.push_back(Pair("error","couldnt get addressutxos"));
        return(result);
    }
    if ( NSPV_utxosresult.total < satoshis+txfee )
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","not enough funds"));
        result.push_back(Pair("balance",(double)NSPV_utxosresult.total/COIN));
        result.push_back(Pair("amount",(double)satoshis/COIN));
        return(result);
    }
    printf("%s numutxos.%d balance %.8f\n",NSPV_utxosresult.coinaddr,NSPV_utxosresult.numutxos,(double)NSPV_utxosresult.total/COIN);
    std::vector<uint8_t> data; CScript opret; std::string hex; struct NSPV_utxoresp used[NSPV_MAXVINS]; CMutableTransaction mtx; CTransaction tx;
    mtx.fOverwintered = true;
    mtx.nExpiryHeight = 0;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;
    if ( ASSETCHAINS_SYMBOL[0] == 0 )
        mtx.nLockTime = (uint32_t)time(NULL) - 777;
    memset(used,0,sizeof(used));
    data.resize(20);
    memcpy(&data[0],&rmd160[1],20);

    if ( NSPV_addinputs(used,mtx,satoshis+txfee,64,NSPV_utxosresult.utxos,NSPV_utxosresult.numutxos) > 0 )
    {
        mtx.vout.push_back(CTxOut(satoshis,CScript() << OP_DUP << OP_HASH160 << ParseHex(HexStr(data)) << OP_EQUALVERIFY << OP_CHECKSIG));
        if ( NSPV_logintime == 0 || time(NULL) > NSPV_logintime+NSPV_AUTOLOGOUT )
        {
            result.push_back(Pair("result","error"));
            result.push_back(Pair("error","wif expired"));
            return(result);
        }
        hex = NSPV_signtx(mtx,txfee,opret,used);
        if ( hex.size() > 0 )
        {
            if ( DecodeHexTx(tx,hex) != 0 )
            {
                TxToJSON(tx,uint256(),result);
                result.push_back(Pair("result","success"));
                result.push_back(Pair("hex",hex));
            }
            else
            {
                result.push_back(Pair("result","error"));
                result.push_back(Pair("error","couldnt decode"));
                result.push_back(Pair("hex",hex));
            }
        }
        else
        {
            result.push_back(Pair("result","error"));
            result.push_back(Pair("error","signing error"));
        }
        return(result);
    }
    else
    {
        result.push_back(Pair("result","error"));
        result.push_back(Pair("error","couldnt create tx"));
        return(result);
    }
}

// polling loop (really this belongs in its own file, but it is so small, it ended up here)

void komodo_nSPV(CNode *pto) // polling loop from SendMessages
{
    uint8_t msg[256]; int32_t i,len=0; uint32_t timestamp = (uint32_t)time(NULL);
    if ( NSPV_logintime != 0 && timestamp > NSPV_logintime+NSPV_AUTOLOGOUT )
    {
        NSPV_logout();
    }
    if ( (pto->nServices & NODE_NSPV) == 0 )
        return;
    if ( KOMODO_NSPV != 0 )
    {
        if ( timestamp > NSPV_lastinfo + ASSETCHAINS_BLOCKTIME/2 && timestamp > pto->prevtimes[NSPV_INFO>>1] + 2*ASSETCHAINS_BLOCKTIME/3 )
        {
            len = 0;
            msg[len++] = NSPV_INFO;
            NSPV_req(pto,msg,len,NODE_NSPV,NSPV_INFO>>1);
        }
    }
}

#endif // KOMODO_NSPVWALLET_H
