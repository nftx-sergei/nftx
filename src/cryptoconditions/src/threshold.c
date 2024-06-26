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
#include "asn/ThresholdFingerprintContents.h"
#include "asn/OCTET_STRING.h"
#include "internal.h"


struct CCType CC_ThresholdType;


static uint32_t thresholdSubtypes(const CC *cond) {
    uint32_t mask = 0;
    for (int i=0; i<cond->size; i++) {
        mask |= cc_typeMask(cond->subconditions[i]);
    }
    mask &= ~(1 << CC_Threshold);
    return mask;
}


static int cmpCostDesc(const void *a, const void *b)
{
    int retval;
    retval = (int) ( *(unsigned long*)b - *(unsigned long*)a );
    return(retval);
    /*if ( retval != 0 )
        return(retval);
    else if ( (uint64_t)a < (uint64_t)b ) // jl777 prevent nondeterminism
        return(-1);
    else return(1);*/
}


static unsigned long thresholdCost(const CC *cond) {
    CC *sub;
    unsigned long *costs = calloc(1, cond->size * sizeof(unsigned long));
    for (int i=0; i<cond->size; i++) {
        sub = cond->subconditions[i];
        costs[i] = cc_getCost(sub);
    }
    qsort(costs, cond->size, sizeof(unsigned long), cmpCostDesc);
    unsigned long cost = 0;
    for (int i=0; i<cond->threshold; i++) {
        cost += costs[i];
    }
    free(costs);
    return cost + 1024 * cond->size;
}


static int thresholdVisitChildren(CC *cond, CCVisitor visitor) {
    for (int i=0; i<cond->size; i++) {
        if (!cc_visit(cond->subconditions[i], visitor)) {
            return 0;
        }
    }
    return 1;
}


static int cmpConditionBin(const void *a, const void *b) {
    /* Compare conditions by their ASN binary representation */
    unsigned char bufa[BUF_SIZE], bufb[BUF_SIZE];
    asn_enc_rval_t r0 = der_encode_to_buffer(&asn_DEF_Condition, *(Condition_t**)a, bufa, BUF_SIZE);
    asn_enc_rval_t r1 = der_encode_to_buffer(&asn_DEF_Condition, *(Condition_t**)b, bufb, BUF_SIZE);

    // below copied from ASN lib
    size_t commonLen = r0.encoded < r1.encoded ? r0.encoded : r1.encoded;
    int ret = memcmp(bufa, bufb, commonLen);

    if (ret == 0)
        return r0.encoded < r1.encoded ? -1 : 1;
    //else if ( (uint64_t)a < (uint64_t)b ) // jl777 prevent nondeterminism
    //    return(-1);
    //else return(1);
    return(0);
}


static void thresholdFingerprint(const CC *cond, uint8_t *out) {
    ThresholdFingerprintContents_t *fp = calloc(1, sizeof(ThresholdFingerprintContents_t));
    fp->threshold = cond->threshold;
    for (int i=0; i<cond->size; i++) {
        Condition_t *asnCond = asnConditionNew(cond->subconditions[i]);
        asn_set_add(&fp->subconditions2, asnCond);
    }
    qsort(fp->subconditions2.list.array, cond->size, sizeof(Condition_t*), cmpConditionBin);
    hashFingerprintContents(&asn_DEF_ThresholdFingerprintContents, fp, out);
}


static int cmpConditionCost(const void *a, const void *b) {
    CC *ca = *((CC**)a);
    CC *cb = *((CC**)b);

    int out = cc_getCost(ca) - cc_getCost(cb);
    if (out != 0) return out;

    // Do an additional sort to establish consistent order
    // between conditions with the same cost.
    Condition_t *asna = asnConditionNew(ca);
    Condition_t *asnb = asnConditionNew(cb);
    out = cmpConditionBin(&asna, &asnb);
    ASN_STRUCT_FREE(asn_DEF_Condition, asna);
    ASN_STRUCT_FREE(asn_DEF_Condition, asnb);
    return out;
}


static CC *thresholdFromFulfillmentMixed(const Fulfillment_t *ffill) {
    ThresholdFulfillment_t *t = ffill->choice.thresholdSha256;
    FulfillmentFlags flags = MixedMode;

    Fulfillment_t** arrFulfills = t->subfulfillments.list.array;
    size_t nffills = t->subfulfillments.list.count;
    size_t nconds = t->subconditions.list.count;

    CC *cond = cc_new(CC_Threshold);

    if (nffills == 0) {
        free(cond);
        //fprintf(stderr, "%s nffills == 0\n", __func__);
        return NULL;
    }

    { // Get the real threshold from the first ffill
        CC *tc = fulfillmentToCC(arrFulfills[0], flags);
        if (tc == NULL) {
            free(cond);
            return NULL;
        }
        if (tc->type->typeId != CC_Preimage || tc->preimageLength != 1) {
            cc_free(tc);
            free(cond);
            return NULL;
        }
        cond->threshold = tc->preimage[0];
        cc_free(tc);
    }

    nffills--;

    if (cond->threshold > nffills + nconds) {
        free(cond);
        return NULL;
    }

    arrFulfills++;

    cond->size = nffills + nconds;
    cond->subconditions = calloc(cond->size, sizeof(CC*));
    

    for (int i=0; i<cond->size; i++) {

        cond->subconditions[i] = (i < nffills) ?
            fulfillmentToCC(arrFulfills[i], flags) :
            mkAnon(t->subconditions.list.array[i-nffills]);

        if (!cond->subconditions[i]) {
            free(cond);
            return NULL;
        }
    }
    return cond;
}


static CC *thresholdFromFulfillment(const Fulfillment_t *ffill, FulfillmentFlags flags) {
    //printf("%s flags & MixedMode %d\n", __func__, (flags & MixedMode));
    if (flags & MixedMode) return thresholdFromFulfillmentMixed(ffill);

    ThresholdFulfillment_t *t = ffill->choice.thresholdSha256;
    int threshold = t->subfulfillments.list.count;
    int size = threshold + t->subconditions.list.count;

    CC **subconditions = calloc(size, sizeof(CC*));

    for (int i=0; i<size; i++) {
        subconditions[i] = (i < threshold) ?
            fulfillmentToCC(t->subfulfillments.list.array[i], 0) :
            mkAnon(t->subconditions.list.array[i-threshold]);

        if (!subconditions[i]) {
            for (int j=0; j<i; j++) free(subconditions[j]);
            free(subconditions);
            //fprintf(stderr, "%s !subconditions[i]\n", __func__);
            return NULL;
        }
    }

    CC *cond = cc_new(CC_Threshold);
    cond->threshold = threshold;
    cond->size = size;
    cond->subconditions = subconditions;
    return cond;
}


static Fulfillment_t *thresholdToFulfillmentMixed(const CC *cond, FulfillmentFlags flags) {
    Fulfillment_t *fulfillment;
    ThresholdFulfillment_t *tf = calloc(1, sizeof(ThresholdFulfillment_t));

    // Add a marker into the threshold to indicate `t`
    CC* t = cc_new(CC_Preimage);
    t->code = calloc(1, 2);
    t->code[0] = cond->threshold; // store threshold value in a special purpose preimage cond
    t->preimageLength = 1;
    asn_set_add(&tf->subfulfillments, asnFulfillmentNew(t, flags));

    for (int i=0; i<cond->size; i++) {
        CC *sub = cond->subconditions[i];
        if (fulfillment = asnFulfillmentNew(sub, flags)) {
            asn_set_add(&tf->subfulfillments, fulfillment);  // always first try to add as fulfillment 
        } else {
            asn_set_add(&tf->subconditions, asnConditionNew(sub));
        }
    }

    fulfillment = calloc(1, sizeof(Fulfillment_t));
    fulfillment->present = Fulfillment_PR_thresholdSha256;
    fulfillment->choice.thresholdSha256 = tf;
    return fulfillment;
}


static Fulfillment_t *thresholdToFulfillment(const CC *cond, FulfillmentFlags flags) {
    if (flags & MixedMode) return thresholdToFulfillmentMixed(cond, flags);

    Fulfillment_t *fulfillment;
    ThresholdFulfillment_t *tf = calloc(1, sizeof(ThresholdFulfillment_t));
    int needed;

    // Make a copy of subconditions so we can leave original order alone
    CC** subconditions = malloc(cond->size*sizeof(CC*));
    memcpy(subconditions, cond->subconditions, cond->size*sizeof(CC*));

    qsort(subconditions, cond->size, sizeof(CC*), cmpConditionCost);
    needed = cond->threshold;

    for (int i=0; i<cond->size; i++) {
        CC *sub = subconditions[i];
        if (needed && !sub->dontFulfill && (fulfillment = asnFulfillmentNew(sub, flags))) {
            asn_set_add(&tf->subfulfillments, fulfillment);  // add as a fulfillment for as many as the thershold number
            needed--;
        } else {
            asn_set_add(&tf->subconditions, asnConditionNew(sub)); // the rest add as anon conds
        }
    }

    free(subconditions);

    if (needed) {
        ASN_STRUCT_FREE(asn_DEF_ThresholdFulfillment, tf);
        return NULL;
    }

    fulfillment = calloc(1, sizeof(Fulfillment_t));
    fulfillment->present = Fulfillment_PR_thresholdSha256;
    fulfillment->choice.thresholdSha256 = tf;
    return fulfillment;
}


static CC *thresholdFromJSON(const cJSON *params, char *err) {
    cJSON *threshold_item = cJSON_GetObjectItem(params, "threshold");
    if (!cJSON_IsNumber(threshold_item)) {
        strcpy(err, "threshold must be a number");
        return NULL;
    }

    cJSON *subfulfillments_item = cJSON_GetObjectItem(params, "subfulfillments");
    if (!cJSON_IsArray(subfulfillments_item)) {
        strcpy(err, "subfulfullments must be an array");
        return NULL;
    }

    CC *cond = cc_new(CC_Threshold);
    cond->threshold = (long) threshold_item->valuedouble;
    cond->size = cJSON_GetArraySize(subfulfillments_item);
    cond->subconditions = calloc(cond->size, sizeof(CC*));

    int dontFulfill = 0;
    cJSON *obj = cJSON_GetObjectItem(params, "dontFulfill");
    if (obj) cond->dontFulfill = !!obj->valueint;

    cJSON *sub;
    for (int i=0; i<cond->size; i++) {
        sub = cJSON_GetArrayItem(subfulfillments_item, i);
        cond->subconditions[i] = cc_conditionFromJSON(sub, err);
        if (/*err[0] || */ cond->subconditions[i]==NULL) // it should not be any 'err' if subconditions was created okay. This 'err[0]' check caused cc_conditionFromJSON failure if err not inited by the user
        {
            if (cond) cc_free(cond);
            return NULL;
        }
    }

    return cond;
}


static void thresholdToJSON(const CC *cond, cJSON *params) {
    cJSON *subs = cJSON_CreateArray();
    cJSON_AddNumberToObject(params, "threshold", cond->threshold);
    for (int i=0; i<cond->size; i++) {
        cJSON_AddItemToArray(subs, cc_conditionToJSON(cond->subconditions[i]));
    }
    cJSON_AddItemToObject(params, "subfulfillments", subs);
}


static int thresholdIsFulfilled(const CC *cond) {
    int nFulfilled = 0;
    for (int i=0; i<cond->size; i++) {
        if (cc_isFulfilled(cond->subconditions[i])) {
            nFulfilled++;
        }
        if (nFulfilled == cond->threshold) {
            return 1;
        }
    }
    return 0;
}


static void thresholdFree(CC *cond) {
    for (int i=0; i<cond->size; i++) {
        cc_free(cond->subconditions[i]);
    }
    free(cond->subconditions);
}

static CC* thresholdCopy(const CC* cond)
{
    CC *condCopy = cc_new(CC_Threshold);
    condCopy->threshold=cond->threshold;
    condCopy->size=cond->size;
    condCopy->subconditions = calloc(condCopy->size, sizeof(CC*));
    for (int i=0; i<cond->size; i++) {
        condCopy->subconditions[i]=cond->subconditions[i]->type->copy(cond->subconditions[i]);
    }
    condCopy->dontFulfill = cond->dontFulfill;
    return (condCopy);
}

struct CCType CC_ThresholdType = { 2, "threshold-sha-256", Condition_PR_thresholdSha256, &thresholdVisitChildren, &thresholdFingerprint, &thresholdCost, &thresholdSubtypes, &thresholdFromJSON, &thresholdToJSON, &thresholdFromFulfillment, &thresholdToFulfillment, &thresholdIsFulfilled, &thresholdFree, &thresholdCopy};
