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

#include "strings.h"
#include "asn/Condition.h"
#include "asn/Fulfillment.h"
#include "asn/OCTET_STRING.h"
#include "../include/cryptoconditions.h"
#include <cJSON.h>
#include "internal.h"
#include "threshold.c"
#include "prefix.c"
#include "preimage.c"
#include "ed25519.c"
#include "secp256k1.c"
#include "secp256k1hash.c"
#include "anon.c"
#include "eval.c"
#include "json_rpc.c"

struct CCType *CCTypeRegistry[] = {
    &CC_PreimageType,
    &CC_PrefixType,
    &CC_ThresholdType,
    NULL, /* &CC_rsaType */
    &CC_Ed25519Type,
    &CC_Secp256k1Type,
    &CC_Secp256k1hashType,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, /* 7-14 unused */
    &CC_EvalType
};


int CCTypeRegistryLength = sizeof(CCTypeRegistry) / sizeof(CCTypeRegistry[0]);


void appendUriSubtypes(uint32_t mask, unsigned char *buf) {
    int append = 0;
    for (int i=0; i<32; i++) {
        if (mask & 1 << i) {
            if (append) {
                strcat(buf, ",");
                strcat(buf, CCTypeRegistry[i]->name);
            } else {
                strcat(buf, "&subtypes=");
                strcat(buf, CCTypeRegistry[i]->name);
                append = 1;
            }
        }
    }
}


char *cc_conditionUri(const CC *cond) {
    unsigned char *fp = calloc(1, 32);
    cond->type->fingerprint(cond, fp);

    unsigned char *encoded = base64_encode_url_safe(fp, 32);

    unsigned char *out = calloc(1, 1000);
    sprintf(out, "ni:///sha-256;%s?fpt=%s&cost=%lu",encoded, cc_typeName(cond), cc_getCost(cond));
    
    if (cond->type->getSubtypes) {
        appendUriSubtypes(cond->type->getSubtypes(cond), out);
    }

    free(fp);
    free(encoded);

    return out;
}


ConditionTypes_t asnSubtypes(uint32_t mask) {
    ConditionTypes_t types;
    memset(&types,0,sizeof(types));
    uint8_t buf[4] = {0,0,0,0};
    int maxId = 0;

    for (int i=0; i<32; i++) {
        if (mask & (1<<i)) {
            maxId = i;
            buf[i >> 3] |= 1 << (7 - i % 8);
        }
    }
    
    types.size = 1 + (maxId >> 3);
    types.buf = calloc(1, types.size);
    memcpy(types.buf, &buf, types.size);
    types.bits_unused = 7 - maxId % 8;
    return types;
}


uint32_t fromAsnSubtypes(const ConditionTypes_t types) {
    uint32_t mask = 0;
    for (int i=0; i<types.size*8; i++) {
        if (types.buf[i >> 3] & (1 << (7 - i % 8))) {
            mask |= 1 << i;
        }
    }
    return mask;
}


size_t cc_conditionBinary(const CC *cond, unsigned char *buf) {  // TODO: make buf size as a param
    Condition_t *asn = calloc(1, sizeof(Condition_t));
    asnCondition(cond, asn);
    size_t out = 0;
    asn_enc_rval_t rc = der_encode_to_buffer(&asn_DEF_Condition, asn, buf, 1000);
    if (rc.encoded == -1) goto end;
    out = rc.encoded;
end:
    ASN_STRUCT_FREE(asn_DEF_Condition, asn);
    return out;
}


size_t cc_fulfillmentBinaryWithFlags(const CC *cond, unsigned char *buf, size_t length, bool flags) {
    Fulfillment_t *ffill = asnFulfillmentNew(cond, flags);
    asn_enc_rval_t rc = der_encode_to_buffer(&asn_DEF_Fulfillment, ffill, buf, length);
    if (rc.encoded == -1) {
        fprintf(stderr, "FULFILLMENT NOT ENCODED\n");
        return 0;
    }
    ASN_STRUCT_FREE(asn_DEF_Fulfillment, ffill);
    return rc.encoded;
}

size_t cc_fulfillmentBinary(const CC *cond, unsigned char *buf, size_t length) {
    return cc_fulfillmentBinaryWithFlags(cond, buf, length, 0);
}

size_t cc_fulfillmentBinaryMixedMode(const CC *cond, unsigned char *buf, size_t length) {
    return cc_fulfillmentBinaryWithFlags(cond, buf, length, MixedMode);
}



void asnCondition(const CC *cond, Condition_t *asn) {
    asn->present = cc_isAnon(cond) ? cond->conditionType->asnType : cond->type->asnType;
    
    // Fixed previous implementation as it was treating every asn as thresholdSha256 type and it was memory leaking
    // because SimpleSha256Condition_t types do not have subtypes so it couldn't free it in the end.
    // int typeId = cond->type->typeId;
    if (asn->present==Condition_PR_thresholdSha256 || asn->present==Condition_PR_prefixSha256)
    {
        CompoundSha256Condition_t* sequence = asn->present == Condition_PR_thresholdSha256 ? &asn->choice.thresholdSha256 : &asn->choice.prefixSha256;
        sequence->cost = cc_getCost(cond);
        sequence->fingerprint.buf = calloc(1, 32);
        cond->type->fingerprint(cond, sequence->fingerprint.buf);
        sequence->fingerprint.size = 32;
        sequence->subtypes = asnSubtypes(cond->type->getSubtypes(cond));
    }
    else
    {
        SimpleSha256Condition_t *choice;
        int fingerprintSize;
        switch (asn->present)
        {
            case Condition_PR_preimageSha256: choice = &asn->choice.preimageSha256; fingerprintSize=32; break;
            case Condition_PR_rsaSha256: choice = &asn->choice.rsaSha256; fingerprintSize=32; break;
            case Condition_PR_ed25519Sha256: choice = &asn->choice.ed25519Sha256; fingerprintSize=32; break;
            case Condition_PR_secp256k1Sha256: choice = &asn->choice.secp256k1Sha256; fingerprintSize=32; break;
            case Condition_PR_secp256k1hashSha256: choice = &asn->choice.secp256k1hashSha256; fingerprintSize=20; break;
            case Condition_PR_evalSha256: choice = &asn->choice.evalSha256; fingerprintSize=32; break;
            default: return;
        };
        choice->cost = cc_getCost(cond);
        choice->fingerprint.buf = calloc(1, 32);
        cond->type->fingerprint(cond, choice->fingerprint.buf);
        choice->fingerprint.size = fingerprintSize;
    }
}


Condition_t *asnConditionNew(const CC *cond) {
    Condition_t *asn = calloc(1, sizeof(Condition_t));
    asnCondition(cond, asn);
    return asn;
}


Fulfillment_t *asnFulfillmentNew(const CC *cond, FulfillmentFlags flags) {
    return cond->type->toFulfillment(cond, flags);
}


unsigned long cc_getCost(const CC *cond) {
    return cond->type->getCost(cond);
}


CCType *getTypeByAsnEnum(Condition_PR present) {
    for (int i=0; i<CCTypeRegistryLength; i++) {
        if (CCTypeRegistry[i] != NULL && CCTypeRegistry[i]->asnType == present) {
            return CCTypeRegistry[i];
        }
    }
    return NULL;
}


CC *fulfillmentToCC(Fulfillment_t *ffill, const FulfillmentFlags flags) {
    CCType *type = getTypeByAsnEnum(ffill->present);
    if (!type) {
        fprintf(stderr, "Unknown fulfillment type: %i\n", ffill->present);
        return 0;
    }
    return type->fromFulfillment(ffill, flags);
}


CC *cc_readFulfillmentBinaryWithFlags(const unsigned char *ffill_bin, size_t ffill_bin_len, FulfillmentFlags flags) {
    CC *cond = 0;
    unsigned char *buf = calloc(1,ffill_bin_len);
    Fulfillment_t *ffill = 0;
    asn_dec_rval_t rval = ber_decode(0, &asn_DEF_Fulfillment, (void **)&ffill, ffill_bin, ffill_bin_len);
    if (rval.code != RC_OK) {
        goto end;
    }
    // Do malleability check
    asn_enc_rval_t rc = der_encode_to_buffer(&asn_DEF_Fulfillment, ffill, buf, ffill_bin_len);
    if (rc.encoded == -1) {
        fprintf(stderr, "FULFILLMENT NOT ENCODED\n");
        goto end;
    }
    if (rc.encoded != ffill_bin_len || 0 != memcmp(ffill_bin, buf, rc.encoded)) {
        goto end;
    }
    
    cond = fulfillmentToCC(ffill, flags);
end:
    free(buf);
    if (ffill) ASN_STRUCT_FREE(asn_DEF_Fulfillment, ffill);
    return cond;
}

CC *cc_readFulfillmentBinary(const unsigned char *ffill_bin, size_t ffill_bin_len) {
    return cc_readFulfillmentBinaryWithFlags(ffill_bin, ffill_bin_len, 0);
}

CC *cc_readFulfillmentBinaryMixedMode(const unsigned char *ffill_bin, size_t ffill_bin_len) {
    return cc_readFulfillmentBinaryWithFlags(ffill_bin, ffill_bin_len, MixedMode);
}

int cc_readFulfillmentBinaryExt(const unsigned char *ffill_bin, size_t ffill_bin_len, CC **ppcc) {
    *ppcc = cc_readFulfillmentBinary(ffill_bin, ffill_bin_len);
    return (*ppcc) ? 0 : -1;
}


int cc_visit(CC *cond, CCVisitor visitor) {
    int out = visitor.visit(cond, visitor);
    if (out && cond->type->visitChildren) {
        out = cond->type->visitChildren(cond, visitor);
    }
    return out;
}

int cc_verify(const struct CC *cond, const unsigned char *msg, size_t msgLength, int doHashMsg,
              const unsigned char *condBin, size_t condBinLength,
              VerifyEval verifyEval, void *evalContext) {
    unsigned char targetBinary[1000];
    //fprintf(stderr,"in cc_verify cond.%p msg.%p[%d] dohash.%d condbin.%p[%d]\n",cond,msg,(int32_t)msgLength,doHashMsg,condBin,(int32_t)condBinLength);
    const size_t binLength = cc_conditionBinary(cond, targetBinary);
    //printf("%s condBin=%s targetBinary=%s\n",  __func__, cc_hex_encode(condBin, condBinLength), cc_hex_encode(targetBinary, binLength));
    if (0 != memcmp(condBin, targetBinary, binLength)) {
        fprintf(stderr,"cc_verify error A (condition != fulfillment)\n");
        return 0;
    }
    if (!cc_ed25519VerifyTree(cond, msg, msgLength)) {
        fprintf(stderr,"cc_verify error B (ed25519Verify)\n");
        return 0;
    }

    unsigned char msgHash[32];
    if (doHashMsg) sha256(msg, msgLength, msgHash);
    else memcpy(msgHash, msg, 32);
    //int32_t z;
    //for (z=0; z<32; z++)
    //    fprintf(stderr,"%02x",msgHash[z]);
    //fprintf(stderr," msgHash msglen.%d\n",(int32_t)msgLength);

    if (!cc_secp256k1VerifyTreeMsg32(cond, msgHash)) {
        fprintf(stderr," cc_verify error C (secp256k1 verify error)\n");
        return 0;
    }

    if (!cc_secp256k1HashVerifyTreeMsg32(cond, msgHash)) {
        fprintf(stderr," cc_verify error C2 (secp256k1hash verify error)\n");
        return 0;
    }

    if (!cc_verifyEval(cond, verifyEval, evalContext)) {
        //fprintf(stderr,"cc_verify error D\n");
        return 0;
    }
    return 1;
}


CC *cc_readConditionBinary(const unsigned char *cond_bin, size_t length) {
    Condition_t *asnCond = 0;
    asn_dec_rval_t rval;
    rval = ber_decode(0, &asn_DEF_Condition, (void **)&asnCond, cond_bin, length);
    if (rval.code != RC_OK) {
        fprintf(stderr, "Failed reading condition binary\n");
        return NULL;
    }
    CC *cond = mkAnon(asnCond);
    ASN_STRUCT_FREE(asn_DEF_Condition, asnCond);
    return cond;
}


int cc_isAnon(const CC *cond) {
    return cond->type->typeId == CC_Anon;
}


enum CCTypeId cc_typeId(const CC *cond) {
    return cc_isAnon(cond) ? cond->conditionType->typeId : cond->type->typeId;
}


uint32_t cc_typeMask(const CC *cond) {
    uint32_t mask = 1 << cc_typeId(cond);
    if (cond->type->getSubtypes)
        mask |= cond->type->getSubtypes(cond);
    return mask;
}


int cc_isFulfilled(const CC *cond) {
    return cond->type->isFulfilled(cond);
}


char *cc_typeName(const CC *cond) {
    return cc_isAnon(cond) ? cond->conditionType->name : cond->type->name;
}


CC *cc_new(int typeId) {
     CC *cond = calloc(1, sizeof(CC));
     cond->type = typeId == CC_Anon ? &CC_AnonType : CCTypeRegistry[typeId];
     return cond;
}


void cc_free(CC *cond) {
    if (cond)
        cond->type->free(cond);
    free(cond);
}

CC* cc_copy(const CC *cond) {
    CC *CCcopy=NULL;
    if (cond)
        CCcopy=cond->type->copy(cond);
    return (CCcopy);
}

int cc_hasSubtypes(enum CCTypeId cctypeid)  {
    return (cctypeid == CC_Threshold || cctypeid == CC_Prefix) ? 1 : 0;
}
