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

#include "asn/Condition.h"
#include "asn/Fulfillment.h"
#include "asn/PrefixFingerprintContents.h"
#include "asn/OCTET_STRING.h"
#include "../include/cryptoconditions.h"


struct CCType CC_AnonType;


CC* cc_anon(const CC *cond) {
    CC *out = cc_new(CC_Anon);
    out->conditionType = cond->type;
    out->cost = cc_getCost(cond);
    out->subtypes = cond->type->getSubtypes(cond);

    unsigned char *fp = calloc(1, 32);
    cond->type->fingerprint(cond,fp);
    memcpy(out->fingerprint, fp, 32);
    free(fp);
    return out;
}

// assumes the root condition is a threshold
CC *mkAnon(const Condition_t *asnCond) {

    CCType *realType = getTypeByAsnEnum(asnCond->present);
    if (!realType) {
        fprintf(stderr, "Unknown ASN type: %i", asnCond->present);
        return 0;
    }
    CC *cond = cc_new(CC_Anon);
    cond->conditionType = realType;
    const CompoundSha256Condition_t *deets = &asnCond->choice.thresholdSha256; // assumes CompoundSha256Condition_t and SimpleSha256Condition_t are same for fingerprint and cost
    memcpy(cond->fingerprint, deets->fingerprint.buf, 32);
    cond->cost = deets->cost;
    if (realType->getSubtypes) {
        cond->subtypes = fromAsnSubtypes(deets->subtypes);
    }
    return cond;
}

static CC* anonFromJSON(const cJSON *params, char *err) {
    size_t len;
    cJSON *fingerprint_item = cJSON_GetObjectItem(params, "fingerprint");
    if (!cJSON_IsString(fingerprint_item)) {
        strcpy(err, "fingerprint must be a string");
        return NULL;
    }
    unsigned char *fing = base64_decode(fingerprint_item->valuestring, &len);
    if (32 != len) {
        strcpy(err, "fingerprint has incorrect length");
        free(fing);
        return NULL;
    }
    cJSON *cost_item = cJSON_GetObjectItem(params, "cost");
    if (!cJSON_IsNumber(cost_item)) {
        strcpy(err, "cost must be a number");
        return NULL;
    }

    /* it was not good to use asnType as it is just an internal utility var:
    cJSON *asnType_item = cJSON_GetObjectItem(params, "asnType"); */
    cJSON *condType_item = cJSON_GetObjectItem(params, "cond_type");
    if (!cJSON_IsNumber(condType_item)) {
        strcpy(err, "cond_type must be a number");
        return NULL;
    }
    if (condType_item->valueint < 0 || condType_item->valueint >= CCTypeRegistryLength)  {
        strcpy(err, "cond_type invalid");
        return NULL;
    }
    CCType * realType = CCTypeRegistry[condType_item->valueint];

    CC* cond=cc_new(CC_Anon);
    memcpy(cond->fingerprint,fing,len);
    cond->cost = (long) cost_item->valuedouble;
    cond->subtypes = 0;
    cond->conditionType = realType;

    if (cc_hasSubtypes(realType->typeId)) {
        cJSON *subtypes_item = cJSON_GetObjectItem(params, "subtypes");
        if (!cJSON_IsNumber(subtypes_item)) {
            strcpy(err, "subtypes must be a number");
            return NULL;
        }
        cond->subtypes = subtypes_item->valueint;
    }
    return cond;
}

static void anonToJSON(const CC *cond, cJSON *params) {
    unsigned char *b64 = base64_encode(cond->fingerprint, 32);
    cJSON_AddItemToObject(params, "fingerprint", cJSON_CreateString(b64));
    free(b64);
    cJSON_AddItemToObject(params, "cost", cJSON_CreateNumber(cond->cost));
    if (cc_hasSubtypes(cc_typeId(cond)))
        cJSON_AddItemToObject(params, "subtypes", cJSON_CreateNumber(cond->subtypes));

    // cJSON_AddItemToObject(params, "asnType", cJSON_CreateNumber((int)cond->conditionType->asnType));
    // this was not good as asnType is not stored into any cc asn1 encoding, 
    // it is just an internal variable for indexing in the asn_MBR_Condition_1 array:
    // better to use a persistent conditionType,
    // the same field is used in the rust cryptocondition lib:
    cJSON_AddItemToObject(params, "cond_type", cJSON_CreateNumber((int) cc_typeId(cond)));
}


static void anonFingerprint(const CC *cond, uint8_t *out) {
    memcpy(out, cond->fingerprint, 32);
}


static unsigned long anonCost(const CC *cond) {
    return cond->cost;
}


static uint32_t anonSubtypes(const CC *cond) {
    return cond->subtypes;
}


static Fulfillment_t *anonFulfillment(const CC *cond, FulfillmentFlags _flags) {
    return NULL;
}


static void anonFree(CC *cond) {
}


static int anonIsFulfilled(const CC *cond) {
    return 0;
}

static CC* anonCopy(const CC* cond)
{
    CC *condCopy = cc_new(CC_Anon);
    memcpy(condCopy->fingerprint,cond->fingerprint,32);
    condCopy->cost = cond->cost;
    condCopy->subtypes = cond->subtypes;
    condCopy->conditionType = cond->conditionType;
    return (condCopy);
}


struct CCType CC_AnonType = { -1, "(anon)", Condition_PR_NOTHING, NULL, &anonFingerprint, &anonCost, &anonSubtypes, &anonFromJSON, &anonToJSON, NULL, &anonFulfillment, &anonIsFulfilled, &anonFree, &anonCopy };
