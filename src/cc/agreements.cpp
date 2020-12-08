/******************************************************************************
 * Copyright © 2014-2020 The SuperNET Developers.                             *
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

#include "CCagreements.h"

/*
The goal here is to create FSM-like on-chain agreements, which are created and their state updated by mutual assent of two separate pubkeys.
Each proposal transaction includes a space for a sha256 hash, which can be used to store a checksum for a legal contract document, point to an oracle/KV transaction, etc.
Contracts are created and their state updated only when the recipient pubkey explicitly accepts an offer/proposal with a separate transaction, somewhat like git pull requests or legally binding contracts.

A visual representation of a typical agreement under this CC would look something like this:

   o                             o - start here
   |                             p - proposal transaction
   V                             c - contract creation transaction
 | p |  | p |  | p |  | p |      u - contract update transaction
   |      |      |      |        t - contract closure transaction
   V      V      V      V
 | c |->| u |->| u |->| t |

 Each contract has a small escrow for a deposit, which is funded by the recipient pubkey at the time of contract creation.
 Some additional features include initial prepayments (can be used for mobilization fees, etc.), as well as optional dispute resolution using a third arbitrator pubkey.

*/

// --- Start of consensus code ---

int64_t IsAgreementsvout(struct CCcontract_info *cp,const CTransaction& tx,int32_t v, char* destaddr)
{
    char tmpaddr[64];
    if ( tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0 )
    {
        if ( Getscriptaddress(tmpaddr,tx.vout[v].scriptPubKey) > 0 && strcmp(tmpaddr,destaddr) == 0 )
            return(tx.vout[v].nValue);
    }
    return(0);
}

// OP_RETURN data encoders and decoders for all Agreements transactions.
CScript EncodeAgreementProposalOpRet(uint8_t version, CPubKey srcpub, CPubKey destpub, std::string agreementname, uint256 agreementhash, int64_t deposit, int64_t payment, uint256 refagreementtxid, bool bNewAgreement, CPubKey arbitratorpub, int64_t disputefee)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'p';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << srcpub << destpub << agreementname << agreementhash << deposit << payment << refagreementtxid << bNewAgreement << arbitratorpub << disputefee);
	return(opret);
}
uint8_t DecodeAgreementProposalOpRet(CScript scriptPubKey, uint8_t &version, CPubKey &srcpub, CPubKey &destpub, std::string &agreementname, uint256 &agreementhash, int64_t &deposit, int64_t &payment, uint256 &refagreementtxid, bool &bNewAgreement, CPubKey &arbitratorpub, int64_t &disputefee)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> srcpub; ss >> destpub; ss >> agreementname; ss >> agreementhash; ss >> deposit; ss >> payment; ss >> refagreementtxid; ss >> bNewAgreement; ss >> arbitratorpub; ss >> disputefee) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementProposalCloseOpRet(uint8_t version, uint256 proposaltxid, CPubKey srcpub, std::string cancelinfo)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 's';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << proposaltxid << srcpub << cancelinfo);
	return(opret);
}
uint8_t DecodeAgreementProposalCloseOpRet(CScript scriptPubKey, uint8_t &version, uint256 &proposaltxid, CPubKey &srcpub, std::string &cancelinfo)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> proposaltxid; ss >> srcpub; ss >> cancelinfo) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementSigningOpRet(uint8_t version, uint256 proposaltxid)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'c';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << proposaltxid);
	return(opret);
}
uint8_t DecodeAgreementSigningOpRet(CScript scriptPubKey, uint8_t &version, uint256 &proposaltxid)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> proposaltxid) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementUpdateOpRet(uint8_t version, uint256 agreementtxid, uint256 proposaltxid)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'u';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << agreementtxid << proposaltxid);
	return(opret);
}
uint8_t DecodeAgreementUpdateOpRet(CScript scriptPubKey, uint8_t &version, uint256 &agreementtxid, uint256 &proposaltxid)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> agreementtxid; ss >> proposaltxid) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementCloseOpRet(uint8_t version, uint256 agreementtxid, uint256 proposaltxid, int64_t depositcut)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 't';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << agreementtxid << proposaltxid << depositcut);
	return(opret);
}
uint8_t DecodeAgreementCloseOpRet(CScript scriptPubKey, uint8_t &version, uint256 &agreementtxid, uint256 &proposaltxid, int64_t &depositcut)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> agreementtxid; ss >> proposaltxid; ss >> depositcut) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementDisputeOpRet(uint8_t version, uint256 agreementtxid, CPubKey srcpub, CPubKey destpub, std::string disputeinfo, bool bFinalDispute)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'd';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << agreementtxid << srcpub << destpub << disputeinfo << bFinalDispute);
	return(opret);
}
uint8_t DecodeAgreementDisputeOpRet(CScript scriptPubKey, uint8_t &version, uint256 &agreementtxid, CPubKey &srcpub, CPubKey &destpub, std::string &disputeinfo, bool &bFinalDispute)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> agreementtxid; ss >> srcpub; ss >> destpub; ss >> disputeinfo; ss >> bFinalDispute) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}

CScript EncodeAgreementDisputeResolveOpRet(uint8_t version, uint256 agreementtxid, uint256 disputetxid, int64_t depositcut, std::string resolutioninfo)
{
	CScript opret; uint8_t evalcode = EVAL_AGREEMENTS, funcid = 'r';
	opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << agreementtxid << disputetxid << depositcut << resolutioninfo);
	return(opret);
}
uint8_t DecodeAgreementDisputeResolveOpRet(CScript scriptPubKey, uint8_t &version, uint256 agreementtxid, uint256 disputetxid, int64_t depositcut, std::string resolutioninfo)
{
	std::vector<uint8_t> vopret; uint8_t evalcode, funcid;
	GetOpReturnData(scriptPubKey, vopret);
	if(vopret.size() > 2 && E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> agreementtxid; ss >> disputetxid; ss >> depositcut; ss >> resolutioninfo) != 0 && evalcode == EVAL_AGREEMENTS)
		return(funcid);
	return(0);
}
// Unlock opret encoders/decoders TBD.

// Generic decoder for Agreements transactions, returns function id.
uint8_t DecodeAgreementOpRet(const CScript scriptPubKey)
{
	std::vector<uint8_t> vopret;
	CPubKey dummypubkey;
	int64_t dummyint64;
	bool dummybool;
	uint256 dummyuint256;
	std::string dummystring;
	uint8_t evalcode, funcid, *script, dummyuint8;
	GetOpReturnData(scriptPubKey, vopret);
	script = (uint8_t *)vopret.data();
	if(script != NULL && vopret.size() > 2)
	{
		evalcode = script[0];
		if (evalcode != EVAL_AGREEMENTS)
		{
			LOGSTREAM("agreementscc", CCLOG_DEBUG1, stream << "script[0] " << script[0] << " != EVAL_AGREEMENTS" << std::endl);
			return (uint8_t)0;
		}
		funcid = script[1];
		LOGSTREAM((char *)"agreementscc", CCLOG_DEBUG2, stream << "DecodeAgreementOpRet() decoded funcId=" << (char)(funcid ? funcid : ' ') << std::endl);
		switch (funcid)
		{
			case 'p':
				return DecodeAgreementProposalOpRet(scriptPubKey, dummyuint8, dummypubkey, dummypubkey, dummystring, dummyuint256, dummyint64, dummyint64, dummyuint256, dummybool, dummypubkey, dummyint64);
			case 't':
				return DecodeAgreementProposalCloseOpRet(scriptPubKey, dummyuint8, dummyuint256, dummypubkey, dummystring);
			case 'c':
				return DecodeAgreementSigningOpRet(scriptPubKey, dummyuint8, dummyuint256);
			case 'u':
				return DecodeAgreementUpdateOpRet(scriptPubKey, dummyuint8, dummyuint256, dummyuint256);
			case 's':
				return DecodeAgreementCloseOpRet(scriptPubKey, dummyuint8, dummyuint256, dummyuint256, dummyint64);
			case 'd':
				return DecodeAgreementDisputeOpRet(scriptPubKey, dummyuint8, dummyuint256, dummypubkey, dummypubkey, dummystring, dummybool);
			case 'r':
				return DecodeAgreementDisputeResolveOpRet(scriptPubKey, dummyuint8, dummyuint256, dummyuint256, dummyint64, dummystring);
			//case 'n':
				// TBD
			default:
				LOGSTREAM((char *)"agreementscc", CCLOG_DEBUG1, stream << "DecodeAgreementOpRet() illegal funcid=" << (int)funcid << std::endl);
				return (uint8_t)0;
		}
	}
	else
		LOGSTREAM("agreementscc",CCLOG_DEBUG1, stream << "not enough opret.[" << vopret.size() << "]" << std::endl);
	return (uint8_t)0;
}

// Validator for normal inputs in Agreements transactions.
bool ValidateAgreementsNormalVins(Eval* eval, const CTransaction& tx, int32_t index)
{
    for (int i=index;i<tx.vin.size();i++)
        if (IsCCInput(tx.vin[i].scriptSig) != 0 )
            return eval->Invalid("vin."+std::to_string(i)+" is normal for agreement tx!");
    return (true);
}

// Validator for CC inputs found in Agreements transactions.
bool ValidateAgreementsCCVin(struct CCcontract_info *cp,Eval* eval,const CTransaction& tx,int32_t index,int32_t prevVout,uint256 prevtxid,char* fromaddr,int64_t amount)
{
    CTransaction prevTx;
	uint256 hashblock;
	int32_t numvouts;
    char tmpaddr[64];

	// Check if a vin is a Agreements CC vin.
    if ((*cp->ismyvin)(tx.vin[index].scriptSig) == 0)
        return eval->Invalid("vin."+std::to_string(index)+" is agreement CC for agreement tx!");

	// Verify previous transaction and its op_return.
    else if (myGetTransaction(tx.vin[index].prevout.hash,prevTx,hashblock) == 0)
        return eval->Invalid("vin."+std::to_string(index)+" tx does not exist!");
    else if ((numvouts=prevTx.vout.size()) > 0 && DecodeAgreementOpRet(prevTx.vout[numvouts-1].scriptPubKey) == 0) 
        return eval->Invalid("invalid vin."+std::to_string(index)+" tx OP_RETURN data!");
	
	// If fromaddr != 0, validate prevout dest address.
    else if (fromaddr!=0 && Getscriptaddress(tmpaddr,prevTx.vout[tx.vin[index].prevout.n].scriptPubKey) && strcmp(tmpaddr,fromaddr)!=0)
        return eval->Invalid("invalid vin."+std::to_string(index)+" address!");

	// if amount > 0, validate amount.
    else if (amount>0 && prevTx.vout[tx.vin[index].prevout.n].nValue!=amount)
        return eval->Invalid("vin."+std::to_string(index)+" invalid amount!");
	
	// if prevVout >= 0, validate spent vout number.
	else if (prevVout>=0 && tx.vin[index].prevout.n!=prevVout)
        return eval->Invalid("vin."+std::to_string(index)+" invalid prevout number, expected "+std::to_string(prevVout)+", got"+std::to_string(tx.vin[index].prevout.n)+"!");
    
	// Validate previous txid.
	else if (prevTx.GetHash()!=prevtxid) 
        return eval->Invalid("invalid vin."+std::to_string(index)+" tx, different txid than expected!");
	
    return (true);
}

// TODO: deprecate
// Finds static (immutable) information from the specified agreement id.
// Static data includes all member pubkeys, deposit, disputefee and refagreementtxid.
// Returns proposaltxid if info is successfully retrieved, zeroid if no agreement is found with the specified txid or the info was not successfully retrieved.
/*uint256 GetAgreementStaticData(uint256 agreementtxid, CPubKey &offerorpub, CPubKey &signerpub, CPubKey &arbitratorpub, int64_t &deposit, int64_t &disputefee, uint256 &refagreementtxid)
{
	uint8_t version;
	int64_t payment;
	CTransaction agreementtx, proposaltx;
	uint256 hashBlock, dummyuint256, proposaltxid;
	std::string dummystring;
	bool dummybool;

	// Finding the agreement transaction.
	if (myGetTransaction(agreementtxid, agreementtx, hashBlock) && agreementtx.vout.size() > 0 && 

	// Decoding the agreement transaction op_return.
	DecodeAgreementSigningOpRet(agreementtxid.vout.back().scriptPubKey,version,proposaltxid) == 'c' &&

	// Finding the accepted proposal transaction.
	myGetTransaction(proposaltxid, proposaltx, hashBlock) && proposaltx.vout.size() > 0 &&

	// Decoding the accepted proposal transaction op_return and getting the agreement data.
	DecodeAgreementProposalOpRet(proposaltx.vout.back().scriptPubKey,version,offerorpub,signerpub,
	dummystring,dummyuint256,deposit,payment,refagreementtxid,dummybool,arbitratorpub,disputefee) == 'p')
	{
		return proposaltxid;
	}

	std::cerr << "GetAgreementStaticData: couldn't find agreement transaction or its data" << std::endl;
	return zeroid;
}*/

// Finds information from the accepted proposal of the specified accept transaction id.
// accepttxid must refer to a transaction with function id 'c', 'u' or 't'.
// Returns proposaltxid if info is successfully retrieved, zeroid if no transaction is found with the specified txid or the info was not successfully retrieved.
uint256 GetAcceptedProposalData(uint256 accepttxid, CPubKey &offerorpub, CPubKey &signerpub, CPubKey &arbitratorpub, int64_t &deposit, int64_t &disputefee, uint256 &refagreementtxid)
{
	uint8_t version, funcid;
	int64_t payment, depositcut;
	CTransaction accepttx, proposaltx;
	uint256 hashBlock, dummyuint256, proposaltxid;
	std::string dummystring;
	bool dummybool;

	// Find specified transaction.
	if (myGetTransaction(accepttxid, accepttx, hashBlock) && accepttx.vout.size() > 0 && 

	// Decode op_return, check function id.
	(funcid = DecodeAgreementOpRet(accepttx.vout.back().scriptPubKey)) != 0)
	{
		switch (funcid)
		{
			// Finding the proposaltxid for various types of transactions.
			case 'c':
				DecodeAgreementSigningOpRet(accepttx.vout.back().scriptPubKey,version,proposaltxid);
				break;
			case 'u':
				DecodeAgreementUpdateOpRet(accepttx.vout.back().scriptPubKey,version,dummyuint256,proposaltxid);
				break;
			case 't':
				DecodeAgreementCloseOpRet(accepttx.vout.back().scriptPubKey,version,dummyuint256,proposaltxid,depositcut);
				break;
			default:
				std::cerr << "GetAcceptedProposalData: incorrect accepttxid funcid "+std::to_string(funcid)+"" << std::endl;
				return zeroid;
		}

		// Finding the accepted proposal transaction.
		if (myGetTransaction(proposaltxid, proposaltx, hashBlock) && proposaltx.vout.size() > 0 &&

		// Decoding the accepted proposal transaction op_return and getting the data.
		DecodeAgreementProposalOpRet(proposaltx.vout.back().scriptPubKey,version,offerorpub,signerpub,
		dummystring,dummyuint256,deposit,payment,refagreementtxid,dummybool,arbitratorpub,disputefee) == 'p')
		{
			return proposaltxid;
		}
	}

	std::cerr << "GetAgreementStaticData: couldn't find accepttxid transaction or its data" << std::endl;
	return zeroid;
}

// Finds the txid of the latest valid transaction that spent the event log baton for the specified agreement.
// Returns agreementtxid if event log baton is unspent, or zeroid if agreement with the specified txid couldn't be found.
uint256 FindLatestAgreementEventTx(uint256 agreementtxid, struct CCcontract_info *cp)
{
	CTransaction sourcetx, batontx;
	uint256 hashBlock, batontxid, refagreementtxid;
	int32_t vini, height, retcode;
	uint8_t funcid;
	char mutualCCaddress[65], arbCCaddress[65];
	CPubKey offerorpub, signerpub, arbitratorpub;
	int64_t deposit, disputefee;

	// Get agreement transaction and its op_return.
	if (myGetTransaction(agreementtxid, sourcetx, hashBlock) && sourcetx.vout.size() > 0 &&
	DecodeAgreementOpRet(sourcetx.vout.back().scriptPubKey) == 'c')
	{
		// Get all member pubkeys of the agreement.
		GetAgreementStaticData(agreementtxid,offerorpub,signerpub,arbitratorpub,deposit,disputefee,refagreementtxid);

		// There are two types of valid event log batons: vouts sent to the offerorpub/signerpub CC 1of2 address with CC_MARKER_VALUE nValue, and vouts sent to the arbitratorpub's CC address.
		// Both of these types are vout.0.
		// To check if a vout is valid, we'll need to get the relevant addresses first.
		GetCCaddress1of2(cp, mutualCCaddress, offerorpub, signerpub);
		GetCCaddress(cp, arbCCaddress, arbitratorpub);

		// Iterate through vout0 batons while we're finding valid Agreements transactions that spent the last baton.
		while ((IsAgreementsvout(cp,sourcetx,0,mutualCCaddress) != CC_MARKER_VALUE || IsAgreementsvout(cp,sourcetx,0,arbCCaddress) != 0) &&
		
		// Check if vout0 was spent.
		(retcode = CCgetspenttxid(batontxid, vini, height, sourcetx.GetHash(), 0)) == 0 &&

		// Get spending transaction and its op_return.
		myGetTransaction(batontxid, batontx, hashBlock) && batontx.vout.size() > 0 && 
		(funcid = DecodeAgreementOpRet(batontx.vout.back().scriptPubKey)) != 0)
		{
			sourcetx = batontx;
		}

		return sourcetx.GetHash();
	}

	return zeroid;
}

// Function for validating proposal ('p' type) transactions.
// Returns true if proposal is valid, otherwise returns eval->Invalid().
bool ValidateAgreementProposalTx(struct CCcontract_info *cp,Eval* eval,const CTransaction& proposaltx,bool bStrict)
{
	uint8_t version;
	int32_t numvins, numvouts;
	int64_t payment, deposit, disputefee, dummyint64;
	CPubKey srcpub, destpub, arbitratorpub, offerorpub, signerpub, refarbitratorpub;
	uint256 agreementhash, refagreementtxid, dummyuint256;
	char mutualCCaddress[65], srcmarker[65], destmarker[65], arbmarker[65];
	bool bNewAgreement;
	std::string agreementname;

	LOGSTREAM("agreementscc", CCLOG_INFO, stream << "ValidateProposalTx: validating proposal essentials" << std::endl);

	// Boundary checks.
	numvins = proposaltx.vin.size();
	numvouts = proposaltx.vout.size();

	CCOpretCheck(eval,proposaltx,true,true,true);
	ExactAmounts(eval,proposaltx,ASSETCHAINS_CCZEROTXFEE[EVAL_AGREEMENTS]?0:CC_TXFEE);

	// Decoding data from last vout.
	if (DecodeAgreementProposalOpRet(proposaltx.vout[numvouts-1].scriptPubKey,version,srcpub,destpub,agreementname,
		agreementhash,deposit,payment,refagreementtxid,bNewAgreement,arbitratorpub,disputefee) != 'p')
		return eval->Invalid("Referenced proposal transaction data invalid!");

	// Verify that vin0 is a normal input and was signed by srcpub.
	else if (ValidateAgreementsNormalVins(eval, proposaltx, 0) == 0)
		return (false);
	else if (TotalPubkeyNormalInputs(proposaltx,srcpub) == 0)
		return eval->Invalid("Found no normal inputs signed by proposal's source pubkey!");

	GetCCaddress1of2(cp, mutualCCaddress, srcpub, destpub);

	// Verify that vout0 is an Agreements vout and is sent to srcpub/destpub 1of2 CC address.
	else if (ConstrainVout(proposaltx.vout[0],1,mutualCCaddress,0)==0)
        return eval->Invalid("vout.0 is agreement CC baton vout to srcpub for 'p' type transaction!");

	// If bStrict is set to true, we also check the proposal data and other vouts.
	// This is to make sure the resulting agreement/update/etc is valid.
	if (bStrict)
	{
		LOGSTREAM("agreementscc", CCLOG_INFO, stream << "ValidateProposalTx: essentials validated, checking proposal data and vouts" << std::endl);

		if (payment < 0)
			return eval->Invalid("Required payment in the proposal must not be negative value!");

		else if (agreementhash == zeroid)
			return eval->Invalid("agreementhash must be specified in the proposal!");

		// srcpub and destpub cannot be the same.
		else if (srcpub == destpub)
			return eval->Invalid("Source pubkey and destination pubkey cannot be the same in proposal!");
		
		// If arbitratorpub is defined, it can't be the same as srcpub or destpub.
		else if (arbitratorpub.IsValid() && (arbitratorpub == srcpub || arbitratorpub == destpub))
			return eval->Invalid("Arbitrator pubkey cannot be the same as srcpub or destpub in proposal!");

		// If refagreementtxid is specified, make sure it is of a valid agreement transaction.
		if (refagreementtxid != zeroid)
		{
			// Get the agreement's offerorpub/signerpub/refarbitratorpub.
			if (GetAgreementStaticData(refagreementtxid,offerorpub,signerpub,refarbitratorpub,dummyint64,dummyint64,dummyuint256) == zeroid)
				return eval->Invalid("Couldn't obtain member pubkeys of ref agreement specified in proposal!");

			// Check if any pubkey specified in the proposal relates to any of the pubkeys specified in the ref agreement.
			else if (srcpub != offerorpub && srcpub != signerpub && destpub != offerorpub && destpub != signerpub)
				return eval->Invalid("Member pubkeys of ref agreement don't match any of the pubkeys specified in proposal!");
		}
		
		// If a proposal has bNewAgreement set, it is treated as a proposal to create a new agreement.
		// Otherwise, it is treated as a proposal to update an existing agreement (defined in refagreementtxid)
		if (bNewAgreement)
		{
			// agreementname must be specified, and it must be <= 64 chars.
			if (agreementname.empty() || agreementname.size() > 64)
				return eval->Invalid("agreementname must be specified in the proposal, and it must be <= 64 chars!");
			
			// If arbitratorpub is defined, disputefee must be at least 10000 sats.
			else if (arbitratorpub.IsValid() && disputefee < CC_MARKER_VALUE)
				return eval->Invalid("disputefee must be >="+std::to_string(CC_MARKER_VALUE)+" satoshis if arbitrator is specified in proposal!");
			
			// Deposit must be at least 10000 sats.
			else if (deposit < CC_MARKER_VALUE)
				return eval->Invalid("deposit must be >="+std::to_string(CC_MARKER_VALUE)+" satoshis in proposal!");
		}
		else
		{
			// If agreementname is specified, it must be <= 64 chars.
			if (!(agreementname.empty()) && agreementname.size() > 64)
				return eval->Invalid("agreementname in the proposal must be <= 64 chars!");

			// refagreementtxid must be specified.
			else if (refagreementtxid == zeroid)
				return eval->Invalid("No agreement specified in proposal for agreement update!");

			else if (!((srcpub == offerorpub && destpub == signerpub) || (destpub == offerorpub && srcpub == signerpub)))
				return eval->Invalid("Pubkey signing the proposal doesn't match any member pubkeys of the agreement!");
			
			// If the accepted proposal for the ref agreement has arbitratorpub defined, this proposal must also have the same pubkey defined.
			else if (refarbitratorpub.IsValid() && arbitratorpub != refarbitratorpub)
				return eval->Invalid("arbitratorpub must be the same as refarbitratorpub in proposal for agreement update!");
		}
		
		GetCCaddress(cp, srcmarker, srcpub);
		GetCCaddress(cp, destmarker, destpub);
		if (arbitratorpub.IsValid())
			GetCCaddress(cp, arbmarker, arbitratorpub);

		// Check vout0 for CC_RESPONSE_VALUE.
		if (ConstrainVout(proposaltx.vout[0],1,mutualCCaddress,CC_RESPONSE_VALUE)==0)
        	return eval->Invalid("vout.0 must be at least "+std::to_string(CC_RESPONSE_VALUE)+" satoshis for 'p' type transaction!");
		
		// vout1 must be a marker sent to srcpub CC address.
		else if (ConstrainVout(proposaltx.vout[1],1,srcmarker,CC_MARKER_VALUE)==0)
        	return eval->Invalid("vout.1 is marker sent to srcpub CC address for 'p' type transaction!");
		
		// vout2 must be a marker sent to destpub CC address.
		else if (ConstrainVout(proposaltx.vout[2],1,destmarker,CC_MARKER_VALUE)==0)
        	return eval->Invalid("vout.2 is marker sent to destpub CC address for 'p' type transaction!");
		
		// If arbitratorpub is specified, vout3 must be a marker sent to arbitratorpub CC address.
		if (arbitratorpub.IsValid())
		{
			if (numvouts < 5 || numvouts > 6)
				return eval->Invalid("Invalid number of vouts for 'p' type transaction with arbitrator!");
			else if (ConstrainVout(proposaltx.vout[3],1,arbmarker,CC_MARKER_VALUE)==0)
        		return eval->Invalid("vout.3 is marker sent to arbitratorpub CC address for 'p' type transaction!");
		}
		else
		{
			if (numvouts < 4 || numvouts > 5)
				return eval->Invalid("Invalid number of vouts for 'p' type transaction without arbitrator!");
		}
	}

	LOGSTREAM("agreementscc", CCLOG_INFO, stream << "ValidateProposalTx: proposal tx validated" << std::endl);

	return (true);
}

bool AgreementsValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{
	CTransaction proposaltx,agreementtx,disputetx;
	CPubKey srcpub,destpub,cancelpub,arbitratorpub,offerorpub,signerpub;
	uint256 proposaltxid,hashBlock,batontxid,agreementhash,agreementtxid,refagreementtxid,disputetxid,disputeagreementtxid;
	int64_t deposit,payment,disputefee,depositcut,proposeddepositcut;
	int32_t numvins,numvouts,vini,height,retcode;
	uint8_t funcid,version;
	std::string cancelinfo,agreementname,disputeinfo,resolutioninfo;
	bool bNewAgreement,bFinalDispute;
	char globalCCaddress[65],mutualCCaddress[65],srcCCaddress[65],srcnormaladdress[65],destCCaddress[65],destnormaladdress[65],arbCCaddress[65];

	// TBD: define CCcontract_info and variables here for integration with other modules.

	// Check boundaries, and verify that input/output amounts are exact.
	numvins = tx.vin.size();
	numvouts = tx.vout.size();
	if (numvouts < 1)
		return eval->Invalid("No vouts!");

	CCOpretCheck(eval,tx,true,true,true);
	ExactAmounts(eval,tx,ASSETCHAINS_CCZEROTXFEE[EVAL_AGREEMENTS]?0:CC_TXFEE);

	// Check the op_return of the transaction and fetch its function id.
	if ((funcid = DecodeAgreementOpRet(tx.vout[numvouts-1].scriptPubKey)) != 0)
	{
		fprintf(stderr,"validating Agreements transaction type (%c)\n",funcid);

		GetCCaddress(cp, globalCCaddress, GetUnspendable(cp, NULL));

		switch (funcid)
		{
			case 'p':
				// Proposal creation:
				// vin.0: normal input
				// ...
				// vin.n-1: normal input
				// vout.0: response baton output to srcpub/destpub CC 1of2 address
				// vout.1: marker to srcpub CC address
				// vout.2: marker to destpub CC address
				// vout.3 (optional): marker to arbitratorpub CC address
				// vout.n-2: normal change
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'p' version srcpub destpub agreementname agreementhash deposit payment refagreementtxid bNewAgreement arbitratorpub disputefee

				// Proposal transactions shouldn't trigger validation directly as they shouldn't contain CC inputs. 
				return eval->Invalid("unexpected AgreementsValidate for 'p' type transaction!");

			case 's':
				// Proposal cancellation:
				// vin.0: normal input
				// vin.1: baton input from proposal
				// ...
				// vin.n-1: normal input
				// vout.n-2: normal change
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 's' version proposaltxid srcpub cancelinfo

				// Get the data from the transaction's op_return.
				DecodeAgreementProposalCloseOpRet(tx.vout[numvouts-1].scriptPubKey,version,proposaltxid,cancelpub,cancelinfo);

				// Check cancelinfo length (should be <= 256 chars).
				if (cancelinfo.size() > 256)
					return eval->Invalid("cancelinfo string over 256 chars!");

				// Get the proposaltxid transaction.
				else if (myGetTransaction(proposaltxid,proposaltx,hashBlock) == 0 || proposaltx.vout.size() == 0 ||
				DecodeAgreementOpRet(proposaltx.vout.back().scriptPubKey) != 'p')
					return eval->Invalid("Cancelled proposal transaction not found for 's' type transaction!");

				// Validate the proposal transaction. (non-strict)
				else if (ValidateAgreementProposalTx(cp,eval,proposaltx,false) == 0)
					return (false);

				// Verify that the proposal hasn't been spent already.
				else if ((retcode = CCgetspenttxid(batontxid, vini, height, proposaltxid, 0)) == 0)
					return eval->Invalid("Cancelled proposal transaction has already been spent!");

				// Get the data from the proposaltxid transaction's op_return.
				DecodeAgreementProposalOpRet(proposaltx.vout.back().scriptPubKey,version,srcpub,destpub,
				agreementname,agreementhash,deposit,payment,refagreementtxid,bNewAgreement,arbitratorpub,disputefee);

				// Verify that this transaction was signed by either srcpub or destpub of specified proposal.
				if (cancelpub != srcpub && cancelpub != destpub)
					return eval->Invalid("Signing pubkey of proposal cancel transaction is not sender or recipient pubkey!");
				
				GetCCaddress1of2(cp, mutualCCaddress, srcpub, destpub);

				// Check vout boundaries for proposal cancel transactions.
				else if (numvouts > 2)
					return eval->Invalid("Too many vouts for 's' type transaction!");

				// Verify that vin.0 was signed by cancelpub.
				else if (IsCCInput(tx.vin[0].scriptSig) != 0 || TotalPubkeyNormalInputs(tx,cancelpub) == 0)
					return eval->Invalid("vin.0 must be normal input signed by transaction creator pubkey!");

				// vin.1 is baton input from proposal
				// Verify that vin.1 was signed correctly.
				else if (ValidateAgreementsCCVin(cp,eval,tx,1,0,proposaltxid,mutualCCaddress,0) == 0)
					return (false);

				// Proposal cancels shouldn't have any additional CC inputs.
				else if (ValidateAgreementsNormalVins(eval,tx,2) == 0)
					return (false);
				
				// Proposal cancels shouldn't have any CC outputs.
				if (tx.vout[0].scriptPubKey.IsPayToCryptoCondition() != 0 || tx.vout[1].scriptPubKey.IsPayToCryptoCondition() != 0)
					return eval->Invalid("Proposal cancels shouldn't have any CC vouts!");
					
				break;
			
			case 'c':
				// Contract creation:
				// vin.0: normal input
				// vin.1: baton input from proposal
				// ...
				// vin.n-1: normal input
				// vout.0: event log baton output to srcpub/destpub CC 1of2 address
				// vout.1: deposit / activity marker to global CC pubkey
				// vout.2: payment to proposal's srcpub normal address
				// vout.3: marker to proposal's srcpub CC address
				// vout.4: marker to proposal's destpub CC address
				// vout.5 (optional): marker to proposal's arbitratorpub CC address
				// vout.n-2: normal change
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'c' version proposaltxid

				// Get the data from the transaction's op_return.
				DecodeAgreementSigningOpRet(tx.vout[numvouts-1].scriptPubKey,version,proposaltxid);

				// Get the proposaltxid transaction.
				if (myGetTransaction(proposaltxid,proposaltx,hashBlock) == 0 || proposaltx.vout.size() == 0 ||
				DecodeAgreementOpRet(proposaltx.vout.back().scriptPubKey) != 'p')
					return eval->Invalid("Accepted proposal transaction not found for 'c' type transaction!");

				// Validate the proposal transaction. (strict)
				else if (ValidateAgreementProposalTx(cp,eval,proposaltx,true) == 0)
					return (false);
				
				// Get the data from the proposaltxid transaction's op_return.
				DecodeAgreementProposalOpRet(proposaltx.vout.back().scriptPubKey,version,srcpub,destpub,
				agreementname,agreementhash,deposit,payment,refagreementtxid,bNewAgreement,arbitratorpub,disputefee);

				// Verify that the proposal is for a new agreement, not for updating an existing agreement.
				if (!bNewAgreement)
					return eval->Invalid("Accepted proposal is not for creating new agreement!");
				
				// Verify that the proposal hasn't been spent already.
				else if ((retcode = CCgetspenttxid(batontxid, vini, height, proposaltxid, 0)) == 0)
					return eval->Invalid("Accepted proposal transaction has already been spent!");
				
				GetCCaddress1of2(cp, mutualCCaddress, srcpub, destpub);
				Getscriptaddress(srcnormaladdress,CScript() << ParseHex(HexStr(srcpub)) << OP_CHECKSIG);
				GetCCaddress(cp, srcCCaddress, srcpub);
				GetCCaddress(cp, destCCaddress, destpub);
				if(arbitratorpub.IsValid())
					GetCCaddress(cp, arbCCaddress, arbitratorpub);

				// Check vout boundaries for contract creation transactions.
				else if (numvouts < (arbitratorpub.IsValid() ? 7 : 6) || numvouts > (arbitratorpub.IsValid() ? 8 : 7))
					return eval->Invalid("Invalid number of vouts for 'c' type transaction!");
				
				// Verify that vin.0 was signed by proposal's destpub.
				else if (IsCCInput(tx.vin[0].scriptSig) != 0 || TotalPubkeyNormalInputs(tx,destpub) == 0)
					return eval->Invalid("vin.0 must be normal input signed by proposal's destination pubkey!");
				
				// Verify that vin.1 was signed correctly.
				else if (ValidateAgreementsCCVin(cp,eval,tx,1,0,proposaltxid,mutualCCaddress,CC_RESPONSE_VALUE) == 0)
					return (false);
				
				// Contract creations shouldn't have any additional CC inputs.
				else if (ValidateAgreementsNormalVins(eval,tx,2) == 0)
					return (false);

				else if (ConstrainVout(tx.vout[0], 1, mutualCCaddress, CC_MARKER_VALUE) == 0)
					return eval->Invalid("vout.0 must be CC baton to proposal's srcpub and destpub CC 1of2 address!");

				// Check if vout1 has correct value assigned. (deposit value specified in proposal)
				else if (ConstrainVout(tx.vout[1], 1, globalCCaddress, deposit) == 0)
					return eval->Invalid("vout.1 must be deposit to agreements global CC address!");

				// Check if vout2 has correct value assigned. (10000 sats + payment specified in proposal)
				else if (ConstrainVout(tx.vout[2], 0, srcnormaladdress, CC_MARKER_VALUE + payment) == 0)
					return eval->Invalid("vout.2 must be "+std::to_string(CC_MARKER_VALUE)+" sats + payment to proposal's srcpub normal address!");

				else if (ConstrainVout(tx.vout[3], 1, srcCCaddress, CC_MARKER_VALUE) == 0)
					return eval->Invalid("vout.3 must be marker to proposal's srcpub CC address!");

				else if (ConstrainVout(tx.vout[4], 1, destCCaddress, CC_MARKER_VALUE) == 0)
					return eval->Invalid("vout.4 must be marker to proposal's destpub CC address!");

				// If arbitratorpub is specified in the proposal, vout5 must be a marker to proposal's arbitratorpub CC address.
				else if (arbitratorpub.IsValid() && ConstrainVout(tx.vout[5], 1, arbCCaddress, CC_MARKER_VALUE) == 0)
					return eval->Invalid("vout.5 must be marker to proposal's arbitratorpub CC address!");
				
				break;
			
			case 'u':
				// Contract update:
				// vin.0: normal input
				// vin.1: baton input from latest agreement event log entry
				// vin.2: baton input from proposal
				// ...
				// vin.n-1: normal input
				// vout.0: event log baton output to srcpub/destpub CC 1of2 address
				// vout.1: payment to proposal's srcpub normal address
				// vout.n-2: normal change
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'u' version agreementtxid proposaltxid

				// Get the data from the transaction's op_return.
				DecodeAgreementUpdateOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,proposaltxid);

				// Get the agreementtxid transaction.
				if (myGetTransaction(agreementtxid,agreementtx,hashBlock) == 0 || agreementtx.vout.size() == 0 ||
				DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Specified agreement not found for 'u' type transaction!");

				// Verify that the agreement is still active by checking if its deposit has been spent or not.
				else if ((retcode = CCgetspenttxid(batontxid, vini, height, agreementtxid, 1)) == 0)
					return eval->Invalid("Agreement specified in update transaction is no longer active!");

				// Get the proposaltxid transaction, and check if it's confirmed/notarised.
				if (myGetTransaction(proposaltxid,proposaltx,hashBlock) == 0 || proposaltx.vout.size() == 0 ||
				DecodeAgreementOpRet(proposaltx.vout.back().scriptPubKey) != 'p')
					return eval->Invalid("Accepted proposal transaction not found for 'u' type transaction!");

				// Validate the proposal transaction. (strict)
				else if (ValidateAgreementProposalTx(cp,eval,proposaltx,true) == 0)
					return (false);

				// Get the data from the proposaltxid transaction's op_return.
				DecodeAgreementProposalOpRet(proposaltx.vout.back().scriptPubKey,version,srcpub,destpub,
				agreementname,agreementhash,deposit,payment,refagreementtxid,bNewAgreement,arbitratorpub,disputefee);

				if (refagreementtxid != agreementtxid)
					return eval->Invalid("Accepted proposal and update transaction are not referring to the same agreement!");
				
				// Verify that the proposal is for updating an existing agreement, not for a new agreement.
				if (bNewAgreement)
					return eval->Invalid("Accepted proposal is not for updating existing agreement!");
				
				// Verifying deposit. (should be < 0 for contract updates)
				if (deposit >= 0)
					return eval->Invalid("Invalid deposit amount for 'u' type transaction, should be -1 or less!");
				
				// Verify that the proposal hasn't been spent already.
				else if ((retcode = CCgetspenttxid(batontxid, vini, height, proposaltxid, 0)) == 0)
					return eval->Invalid("Accepted proposal transaction has already been spent!");
				
				GetCCaddress1of2(cp, mutualCCaddress, srcpub, destpub);
				Getscriptaddress(srcnormaladdress,CScript() << ParseHex(HexStr(srcpub)) << OP_CHECKSIG);

				// Check vout boundaries for contract update transactions.
				else if (numvouts < 3 || numvouts > 4)
					return eval->Invalid("Invalid number of vouts for 'u' type transaction!");

				// Verify that vin.0 was signed by proposal's destpub.
				else if (IsCCInput(tx.vin[0].scriptSig) != 0 || TotalPubkeyNormalInputs(tx,destpub) == 0)
					return eval->Invalid("vin.0 must be normal input signed by proposal's destination pubkey!");

				// Verify that vin.1 is spending the event log baton from the latest agreement event.
				else if (ValidateAgreementsCCVin(cp,eval,tx,1,0,FindLatestAgreementEventTx(agreementtxid,cp),mutualCCaddress,CC_MARKER_VALUE) == 0)
					return (false);

				// Verify that vin.2 was signed correctly & is spending the proposal response baton.
				else if (ValidateAgreementsCCVin(cp,eval,tx,2,0,proposaltxid,mutualCCaddress,CC_RESPONSE_VALUE) == 0)
					return (false);

				// Contract creations shouldn't have any additional CC inputs.
				else if (ValidateAgreementsNormalVins(eval,tx,3) == 0)
					return (false);

				else if (ConstrainVout(tx.vout[0], 1, mutualCCaddress, CC_MARKER_VALUE) == 0)
					return eval->Invalid("vout.0 must be CC baton to proposal's srcpub and destpub CC 1of2 address!");
				
				// Check if vout1 has correct value assigned. (10000 sats + payment specified in proposal)
				else if (ConstrainVout(tx.vout[1], 0, srcnormaladdress, CC_MARKER_VALUE + payment) == 0)
					return eval->Invalid("vout.1 must be "+std::to_string(CC_MARKER_VALUE)+" sats + payment to proposal's srcpub normal address!");

				break;

			case 't':
				// Contract closure:
				// vin.0: normal input
				// vin.1: baton input from latest agreement event log entry
				// vin.2: baton input from proposal
				// vin.3: deposit from agreement
				// ...
				// vin.n-1: normal input
				// vout.0: depositcut + payment to proposal's srcpub normal address
				// vout.n-2: normal change + (deposit - depositcut)
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 't' version agreementtxid proposaltxid depositcut

				// Get the data from the transaction's op_return.
				DecodeAgreementCloseOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,proposaltxid,depositcut);

				// Get the agreementtxid transaction.
				if (myGetTransaction(agreementtxid,agreementtx,hashBlock) == 0 || agreementtx.vout.size() == 0 ||
				DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Specified agreement not found for 't' type transaction!");

				// Verify that the agreement is still active by checking if its deposit has been spent or not.
				else if ((retcode = CCgetspenttxid(batontxid, vini, height, agreementtxid, 1)) == 0)
					return eval->Invalid("Agreement specified in closure transaction is no longer active!");

				// Check value of the deposit vout, for depositcut checks later on.
				deposit = agreementtx.vout[1].nValue;

				// Get the proposaltxid transaction, and check if it's confirmed/notarised.
				if (myGetTransaction(proposaltxid,proposaltx,hashBlock) == 0 || proposaltx.vout.size() == 0 ||
				DecodeAgreementOpRet(proposaltx.vout.back().scriptPubKey) != 'p')
					return eval->Invalid("Accepted proposal transaction not found for 't' type transaction!");

				// Validate the proposal transaction. (strict)
				else if (ValidateAgreementProposalTx(cp,eval,proposaltx,true) == 0)
					return (false);

				// Get the data from the proposaltxid transaction's op_return.
				DecodeAgreementProposalOpRet(proposaltx.vout.back().scriptPubKey,version,srcpub,destpub,agreementname,
				agreementhash,proposeddepositcut,payment,refagreementtxid,bNewAgreement,arbitratorpub,disputefee);

				if (refagreementtxid != agreementtxid)
					return eval->Invalid("Accepted proposal and closure transaction are not referring to the same agreement!");
				
				// Verify that the proposal is for updating an existing agreement, not for a new agreement.
				if (bNewAgreement)
					return eval->Invalid("Accepted proposal is not for updating existing agreement!");

				// Verify that the proposal hasn't been spent already.
				else if ((retcode = CCgetspenttxid(batontxid, vini, height, proposaltxid, 0)) == 0)
					return eval->Invalid("Accepted proposal transaction has already been spent!");
				
				// Verify that depositcut is <= deposit, and that it matches with what's in the proposal.
				if (depositcut != proposeddepositcut)
					return eval->Invalid("Deposit cut in 't' type transaction does not match the proposed deposit cut!");

				// Verifying deposit cut. (should be >= 0 for contract closures)
				if (depositcut < 0 || depositcut > deposit)
					return eval->Invalid("Invalid depositcut amount for 't' type transaction, should be between 0 and total deposit!");
				
				GetCCaddress1of2(cp, mutualCCaddress, srcpub, destpub);
				Getscriptaddress(srcnormaladdress,CScript() << ParseHex(HexStr(srcpub)) << OP_CHECKSIG);

				// Check vout boundaries for contract closure transactions.
				else if (numvouts < 2 || numvouts > 3)
					return eval->Invalid("Invalid number of vouts for 't' type transaction!");

				// Verify that vin.0 was signed by proposal's destpub.
				else if (IsCCInput(tx.vin[0].scriptSig) != 0 || TotalPubkeyNormalInputs(tx,destpub) == 0)
					return eval->Invalid("vin.0 must be normal input signed by proposal's destination pubkey!");

				// Verify that vin.1 is spending the event log baton from the latest agreement event.
				else if (ValidateAgreementsCCVin(cp,eval,tx,1,0,FindLatestAgreementEventTx(agreementtxid,cp),mutualCCaddress,CC_MARKER_VALUE) == 0)
					return (false);

				// Verify that vin.2 was signed correctly & is spending the proposal response baton.
				else if (ValidateAgreementsCCVin(cp,eval,tx,2,0,proposaltxid,mutualCCaddress,CC_RESPONSE_VALUE) == 0)
					return (false);

				// Verify that vin.3 is spending the deposit from the specified agreement.
				else if (ValidateAgreementsCCVin(cp,eval,tx,3,0,agreementtxid,globalCCaddress,deposit) == 0)
					return (false);

				// Contract closures shouldn't have any additional CC inputs.
				else if (ValidateAgreementsNormalVins(eval,tx,4) == 0)
					return (false);

				// Check if vout0 has correct value assigned. (10000 sats + depositcut + payment value specified in proposal)
				else if (ConstrainVout(tx.vout[0], 0, srcnormaladdress, CC_MARKER_VALUE + depositcut + payment) == 0)
					return eval->Invalid("vout.0 must be "+std::to_string(CC_MARKER_VALUE)+" sats + depositcut + payment to proposal's srcpub normal address!");

				// NOTE: should there be a check to make sure vout1 has enough nValue to be at least deposit-depositcut?

				break;
			
			case 'd':
				// Contract dispute:
				// vin.0: normal input
				// vin.1: baton input from latest agreement event log entry
				// ...
				// vin.n-1: normal input
				// vout.0: event log baton output + disputefee to arbitratorpub CC address
				// vout.n-2: normal change
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'd' version agreementtxid srcpub destpub disputeinfo bFinalDispute

				// Get the data from the transaction's op_return.
				DecodeAgreementDisputeOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,srcpub,destpub,disputeinfo,bFinalDispute);

				// Check disputeinfo length (should be <= 256 chars).
				if (disputeinfo.size() > 256)
					return eval->Invalid("disputeinfo string over 256 chars!");

				// Get the agreementtxid transaction.
				else if (myGetTransaction(agreementtxid,agreementtx,hashBlock) == 0 || agreementtx.vout.size() == 0 ||
				DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Specified agreement not found for 'd' type transaction!");

				// Verify that the agreement is still active by checking if its deposit has been spent or not.
				else if ((retcode = CCgetspenttxid(batontxid, vini, height, agreementtxid, 1)) == 0)
					return eval->Invalid("Agreement specified in dispute transaction is no longer active!");

				// Check if it contains an arbitrator pubkey. (aka if disputes are enabled)
				GetAgreementStaticData(agreementtxid,offerorpub,signerpub,arbitratorpub,deposit,disputefee,refagreementtxid);
				if (!(arbitratorpub.IsValid()))
					return eval->Invalid("Agreement arbitrator pubkey invalid or unspecified, disputes are disabled!");

				// Get the agreement's offeror and signer pubkeys, check if they match the srcpub/destpub pubkeys.
				else if (!((srcpub == offerorpub && destpub == signerpub) || (destpub == offerorpub && srcpub == signerpub)))
					return eval->Invalid("Pubkey signing the dispute doesn't match any member pubkeys of the agreement!");

				GetCCaddress1of2(cp, mutualCCaddress, srcpub, destpub);
				GetCCaddress(cp, arbCCaddress, arbitratorpub);

				// Check vout boundaries for contract dispute transactions.
				else if (numvouts < 2 || numvouts > 3)
					return eval->Invalid("Invalid number of vouts for 'd' type transaction!");

				// Verify that vin.0 was signed by srcpub.
				else if (IsCCInput(tx.vin[0].scriptSig) != 0 || TotalPubkeyNormalInputs(tx,srcpub) == 0)
					return eval->Invalid("vin.0 must be normal input signed by dispute source pubkey!");

				// Verify that vin.1 is spending the event log baton from the latest agreement event.
				else if (ValidateAgreementsCCVin(cp,eval,tx,1,0,FindLatestAgreementEventTx(agreementtxid,cp),mutualCCaddress,CC_MARKER_VALUE) == 0)
					return (false);
					
				// Contract disputes shouldn't have any additional CC inputs.
				else if (ValidateAgreementsNormalVins(eval,tx,2) == 0)
					return (false);

				// Check if vout0 has correct value assigned. (10000 sats + disputefee value specified in dispute)
				else if (ConstrainVout(tx.vout[0], 1, arbCCaddress, CC_MARKER_VALUE + disputefee) == 0)
					return eval->Invalid("vout.0 must be "+std::to_string(CC_MARKER_VALUE)+" sats + disputefee to agreement's arbitratorpub CC address!");

				break;

			case 'r':
				// Contract dispute resolution:
				// vin.0: normal input
				// vin.1: baton input from latest agreement event log entry + disputefee
				// vin.2 (optional): deposit from agreement
				// ...
				// vin.n-1: normal input
				// if deposit is taken:
				// 	vout.0: depositcut to dispute's srcpub normal address, assuming depositcut != 0
				// 	vout.1: (deposit - depositcut) to dispute's destpub normal address, assuming depositcut != deposit
				// if deposit is depositcut < 0:
				// 	vout.0: event log baton output to srcpub/destpub CC 1of2 address
				// vout.n-2: normal change + disputefee
				// vout.n-1: OP_RETURN EVAL_AGREEMENTS 'r' version agreementtxid disputetxid depositcut resolutioninfo

				// Get the data from the transaction's op_return.
				DecodeAgreementDisputeResolveOpRet(tx.vout[numvouts-1].scriptPubKey,version,agreementtxid,disputetxid,depositcut,resolutioninfo);

				// Check resolutioninfo length (should be <= 256 chars).
				if (resolutioninfo.size() > 256)
					return eval->Invalid("resolutioninfo string over 256 chars!");

				// Get the agreement transaction and check if it contains an arbitrator pubkey. (aka if disputes are enabled)
				else if (myGetTransaction(agreementtxid,agreementtx,hashBlock) == 0 || agreementtx.vout.size() == 0 ||
				DecodeAgreementOpRet(agreementtx.vout.back().scriptPubKey) != 'c')
					return eval->Invalid("Specified agreement not found for 'r' type transaction!");

				// Verify that the agreement is still active by checking if its deposit has been spent or not.
				else if ((retcode = CCgetspenttxid(batontxid, vini, height, agreementtxid, 1)) == 0)
					return eval->Invalid("Agreement specified in dispute transaction is no longer active!");

				// Get the agreement deposit value.
				GetAgreementStaticData(agreementtxid,offerorpub,signerpub,arbitratorpub,deposit,disputefee,refagreementtxid);
				if (!(arbitratorpub.IsValid()))
					return eval->Invalid("Agreement arbitrator pubkey invalid or unspecified, disputes are disabled!");
				
				// Verify that depositcut <= deposit.
				if (depositcut > deposit)
					return eval->Invalid("Invalid depositcut amount for 'r' type transaction, should be between 0 and total deposit!");

				// Get the dispute transaction, and check if it's confirmed/notarised.
				else if (myGetTransaction(disputetxid,disputetx,hashBlock) == 0 || disputetx.vout.size() == 0 ||
				DecodeAgreementOpRet(disputetx.vout.back().scriptPubKey) != 'd')
					return eval->Invalid("Specified dispute not found for 'r' type transaction!");

				// Verify that the dispute is valid. (points to correct agreementtxid, srcpub/destpub is correct etc.)
				if (FindLatestAgreementEventTx(agreementtxid,cp) != disputetxid)
					return eval->Invalid("Specified dispute txid is not latest event for this agreement!");

				// Get dispute srcpub and destpub pubkeys.
				DecodeAgreementDisputeOpRet(disputetx.vout[numvouts-1].scriptPubKey,version,disputeagreementtxid,srcpub,destpub,disputeinfo,bFinalDispute);

				if (disputeagreementtxid != agreementtxid)
					return eval->Invalid("Dispute and dispute resolution are referring to different agreement txids!");

				else if (!((srcpub == offerorpub && destpub == signerpub) || (destpub == offerorpub && destpub == signerpub)))
					return eval->Invalid("Pubkey signing the dispute doesn't match any member pubkeys of the agreement!");

				// If depositcut < 0, then the deposit vout from the agreement must not be spent.
				if (depositcut < 0 && bFinalDispute)
					return eval->Invalid("Cannot cancel final dispute for 'r' type transaction!");

				GetCCaddress1of2(cp, mutualCCaddress, srcpub, destpub);
				Getscriptaddress(srcnormaladdress,CScript() << ParseHex(HexStr(srcpub)) << OP_CHECKSIG);
				Getscriptaddress(destnormaladdress,CScript() << ParseHex(HexStr(destpub)) << OP_CHECKSIG);
				GetCCaddress(cp, arbCCaddress, arbitratorpub);
				
				// Check boundaries for dispute resolution transactions.
				else if (numvouts < 2 || numvouts > 4)
					return eval->Invalid("Invalid number of vouts for 'd' type transaction!");
				
				// Verify that vin.0 was signed by arbitratorpub.
				else if (IsCCInput(tx.vin[0].scriptSig) != 0 || TotalPubkeyNormalInputs(tx,arbitratorpub) == 0)
					return eval->Invalid("vin.0 must be normal input signed by agreement arbitrator pubkey!");

				// Verify that vin.1 is spending the event log baton from the dispute.
				else if (ValidateAgreementsCCVin(cp,eval,tx,1,0,disputetxid,arbCCaddress,CC_MARKER_VALUE+disputefee) == 0)
					return (false);
				
				// If depositcut >= 0, verify that vin.2 is spending the deposit from the agreement.
				if (depositcut >= 0)
				{
					if (ValidateAgreementsCCVin(cp,eval,tx,2,1,agreementtxid,globalCCaddress,deposit) == 0)
						return (false);
					else if (ValidateAgreementsNormalVins(eval,tx,3) == 0)
						return (false);
				}

				// If depositcut < 0, make sure there are no additional CC inputs.
				else
				{
					if (ValidateAgreementsNormalVins(eval,tx,2) == 0)
						return (false);
				} 

				// If depositcut < 0, verify that vout0 (event log) was sent as CC output to correct address.
				if (depositcut < 0)
				{
					if (ConstrainVout(tx.vout[0], 1, mutualCCaddress, CC_MARKER_VALUE) == 0)
						return eval->Invalid("vout.0 must be CC baton to proposal's srcpub and destpub CC 1of2 address if depositcut < 0!");
				}

				// If depositcut == 0, check if vout0 value is equal to deposit and if it was sent to dispute's destpub normal address.
				else if (depositcut == 0)
				{
					if (ConstrainVout(tx.vout[0], 0, destnormaladdress, deposit) == 0)
						return eval->Invalid("vout.0 must be deposit to dispute destpub normal address if depositcut == 0!");
				}

				// If depositcut > 0, check if vout0 value is equal to depositcut and is sent to dispute's srcpub normal address.
				else if (depositcut > 0)
				{
					if (ConstrainVout(tx.vout[0], 0, srcnormaladdress, depositcut) == 0)
						return eval->Invalid("vout.0 must be depositcut to dispute srcpub normal address if depositcut > 0!");

					// If depositcut != deposit, check if vout1 is equal to (deposit - depositcut) and if it was sent to dispute's destpub normal address.
					if (depositcut != deposit)
					{
						if (ConstrainVout(tx.vout[1], 0, destnormaladdress, deposit - depositcut) == 0)
							return eval->Invalid("vout.1 must be (deposit - depositcut) to dispute srcpub normal address if depositcut > 0 & depositcut != deposit!");
					}
				}

				// NOTE: do we check to make sure arbitrator gets >= disputefee to their normal address?

				break;
			
			case 'n':
				// Contract deposit unlock:
				// TBD
				// OP_RETURN EVAL_AGREEMENTS 'n' version agreementtxid unlocktxid
				return eval->Invalid("Unexpected Agreements 'n' function id, not built yet!");
		
			default:
				fprintf(stderr,"unexpected agreements funcid (%c)\n",funcid);
				return eval->Invalid("Unexpected Agreements function id!");
		}
	}
	else
		return eval->Invalid("Invalid Agreements function id and/or data!");

	LOGSTREAM("agreementscc", CCLOG_INFO, stream << "Agreements transaction validated" << std::endl);
	return (true);
}

// --- End of consensus code ---

// --- Helper functions for RPC implementations ---

// TODO: GetLatestAgreementName
std::string GetLatestAgreementName(uint256 agreementtxid)
{

}

// TODO: GetLatestAgreementHash
uint256 GetLatestAgreementName(uint256 agreementtxid)
{
	
}

// agreementcreate receiver agreementname agreementhash deposit [arbitrator][disputefee][refagreementtxid][payment]
UniValue AgreementCreate(const CPubKey& pk,uint64_t txfee,CPubKey destpub,std::string agreementname,uint256 agreementhash,int64_t deposit,CPubKey arbitratorpub,int64_t disputefee,uint256 refagreementtxid,int64_t payment)
{
	// Variables

	CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
	struct CCcontract_info *cp,C; cp = CCinit(&C,EVAL_AGREEMENTS);
	if (txfee == 0) txfee = CC_TXFEE;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());

	// Parse data, check validity and get info needed for tx construction.
	// Check receiver.

	// Check name.

	// Check hash.

	// Check deposit, make sure it's >= 10000 sats (TODO: should we check max value?)

	// Check arbitrator, if it exists:
		// Check disputefee, make sure it's >= 10000 sats (TODO: should we check max value?)
	// Else, set disputefee to 0

	// Check refagreement, if it exists verify it's used correctly and stuff

	// Check payment if it exists, make sure it's >= 0 (TODO: should we check max value?)

	// Construct opret.

	if (AddNormalinputs2(mtx, txfee + CC_RESPONSE_VALUE + CC_MARKER_VALUE * 3, 60) > 0) // vin.*: normal input
	{
		// vout.0: response baton output to srcpub/destpub CC 1of2 address
		mtx.vout.push_back(MakeCC1of2vout(EVAL_AGREEMENTS, CC_RESPONSE_VALUE, mypk, CPK_dest)); 
		// vout.1: marker to srcpub CC address
		mtx.vout.push_back(MakeCC1vout(EVAL_AGREEMENTS, CC_MARKER_VALUE, mypk)); 
		// vout.2: marker to destpub CC address
		mtx.vout.push_back(MakeCC1vout(EVAL_AGREEMENTS, CC_MARKER_VALUE, destpub)); 
		if (arbitratorpub.IsFullyValid())
			// vout.3 (optional): marker to arbitratorpub CC address
			mtx.vout.push_back(MakeCC1vout(EVAL_AGREEMENTS, CC_MARKER_VALUE, destpub)); 
	
		return FinalizeCCTxExt(pk.IsValid(),0,cp,mtx,mypk,txfee,opret);
	}

	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "error adding normal inputs, check if you have available funds");
}

// Implementations:
// 	agreementcreate receiver agreementname agreementhash deposit [arbitrator][disputefee][refagreementtxid][payment]
// 	agreementupdate agreementtxid agreementhash [agreementname][payment]
// 	agreementclose agreementtxid agreementhash depositcut [agreementname][payment]
//	agreementstopproposal proposaltxid
// agreementaccept proposaltxid
// 	agreementdispute agreementtxid disputeinfo [bFinalDispute]
// 	agreementresolve disputetxid depositcut
// 	agreementunlock agreementtxid unlocktxid

//===========================================================================
// RPCs - informational
//===========================================================================
/*UniValue AgreementInfo(uint256 txid)
{
	UniValue result(UniValue::VOBJ), members(UniValue::VOBJ), data(UniValue::VOBJ);
	CPubKey CPK_src, CPK_dest, CPK_arbitrator;
	CTransaction tx, proposaltx;
	uint256 hashBlock, datahash, proposaltxid, prevproposaltxid, agreementtxid, latesttxid, spendingtxid, dummytxid, pawnshoptxid;
	std::vector<uint8_t> srcpub, destpub, arbitrator, rewardedpubkey;
	int32_t numvouts;
	int64_t payment, disputefee, deposit, totaldeposit, revision;
	std::string name, CCerror;
	bool bHasReceiver, bHasArbitrator;
	uint8_t funcid, version, proposaltype, updatefuncid, spendingfuncid, mypriv[32];
	char mutualaddr[65];
	if (myGetTransaction(txid,tx,hashBlock) != 0 && (numvouts = tx.vout.size()) > 0 &&
	(funcid = DecodeAgreementOpRet(tx.vout[numvouts-1].scriptPubKey)) != 0)
	{
		// Only show info for confirmed transactions.
		if (KOMODO_NSPV_FULLNODE && hashBlock.IsNull())
			CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "the transaction is still in mempool");
		
		result.push_back(Pair("result", "success"));
		result.push_back(Pair("txid", txid.GetHex()));
		switch (funcid)
		{
			case 'p':
				result.push_back(Pair("type","proposal"));
				DecodeAgreementProposalOpRet(tx.vout[numvouts-1].scriptPubKey,version,proposaltype,srcpub,destpub,arbitrator,payment,disputefee,deposit,datahash,agreementtxid,prevproposaltxid,name);
				CPK_src = pubkey2pk(srcpub);
				CPK_dest = pubkey2pk(destpub);
				CPK_arbitrator = pubkey2pk(arbitrator);
				bHasReceiver = CPK_dest.IsFullyValid();
				bHasArbitrator = CPK_arbitrator.IsFullyValid();
				members.push_back(Pair("sender",HexStr(srcpub)));
				if (bHasReceiver)
					members.push_back(Pair("receiver",HexStr(destpub)));
				if (payment > 0)
					data.push_back(Pair("required_payment", (double)payment/COIN));
				data.push_back(Pair("contract_name",name));
				data.push_back(Pair("contract_hash",datahash.GetHex()));
				switch (proposaltype)
				{
					case 'p':
						result.push_back(Pair("proposal_type","contract_create"));
						if (bHasArbitrator)
						{
							members.push_back(Pair("arbitrator",HexStr(arbitrator)));
							data.push_back(Pair("dispute_fee",(double)disputefee/COIN));
						}
						data.push_back(Pair("deposit",(double)deposit/COIN));
						if (agreementtxid != zeroid)
							data.push_back(Pair("master_contract_txid",agreementtxid.GetHex()));
						break;
					case 'u':
						result.push_back(Pair("proposal_type","contract_update"));
						result.push_back(Pair("contract_txid",agreementtxid.GetHex()));
						if (bHasArbitrator)
						{
							data.push_back(Pair("new_dispute_fee", (double)disputefee/COIN));
							GetAgreementInitialData(agreementtxid, proposaltxid, srcpub, destpub, arbitrator, disputefee, deposit, datahash, dummytxid, name);
							GetLatestAgreementUpdate(agreementtxid, latesttxid, updatefuncid);
							GetAgreementUpdateData(latesttxid, name, datahash, disputefee, deposit, revision);
							data.push_back(Pair("current_dispute_fee", (double)disputefee/COIN));
						}
						break;
					case 't':
						result.push_back(Pair("proposal_type","contract_close"));
						result.push_back(Pair("contract_txid",agreementtxid.GetHex()));
						GetAgreementInitialData(agreementtxid, proposaltxid, srcpub, destpub, arbitrator, disputefee, totaldeposit, datahash, dummytxid, name);
						data.push_back(Pair("deposit_for_sender", (double)deposit/COIN));
						data.push_back(Pair("deposit_for_receiver", (double)(totaldeposit-deposit)/COIN));
						data.push_back(Pair("total_deposit", (double)totaldeposit/COIN));
						break;
				}
				result.push_back(Pair("members",members));
				if (IsProposalSpent(txid, spendingtxid, spendingfuncid))
				{
					switch (spendingfuncid)
					{
						case 'p':
							result.push_back(Pair("status","updated"));
							break;
						case 'c':
						case 'u':
						case 's':
							result.push_back(Pair("status","accepted"));
							break;
						case 't':
							result.push_back(Pair("status","closed"));
							break;
					}
					result.push_back(Pair("next_txid",spendingtxid.GetHex()));
				}
				else
				{
					if (bHasReceiver)
						result.push_back(Pair("status","open"));
					else
						result.push_back(Pair("status","draft"));
				}
				if (prevproposaltxid != zeroid)
					result.push_back(Pair("previous_txid",prevproposaltxid.GetHex()));
				result.push_back(Pair("data",data));
				break;
			case 't':
				result.push_back(Pair("type","proposal cancel"));
				DecodeAgreementProposalCloseOpRet(tx.vout[numvouts-1].scriptPubKey, version, proposaltxid, srcpub);
				result.push_back(Pair("source_pubkey",HexStr(srcpub)));
				result.push_back(Pair("proposal_txid",proposaltxid.GetHex()));
				break;
			case 'c':
				result.push_back(Pair("type","contract"));
				DecodeAgreementSigningOpRet(tx.vout[numvouts-1].scriptPubKey, version, proposaltxid);
				GetAgreementInitialData(txid, proposaltxid, srcpub, destpub, arbitrator, disputefee, deposit, datahash, agreementtxid, name);
				CPK_arbitrator = pubkey2pk(arbitrator);
				bHasArbitrator = CPK_arbitrator.IsFullyValid();
				result.push_back(Pair("accepted_txid",proposaltxid.GetHex()));
				members.push_back(Pair("initiator",HexStr(srcpub)));
				members.push_back(Pair("recipient",HexStr(destpub)));
				result.push_back(Pair("deposit",deposit));
				if (bHasArbitrator)
				{	
					members.push_back(Pair("arbitrator",HexStr(arbitrator)));
				}
				result.push_back(Pair("members",members));
				if (agreementtxid != zeroid)
					data.push_back(Pair("master_contract_txid",agreementtxid.GetHex()));
				GetLatestAgreementUpdate(txid, latesttxid, updatefuncid);
				if (latesttxid != txid)
				{
					switch (updatefuncid)
					{
						case 'u':
							result.push_back(Pair("status","updated"));
							break;
						case 's':
							result.push_back(Pair("status","closed"));
							break;
						case 'd':
							result.push_back(Pair("status","suspended"));
							break;
						case 'r':
							result.push_back(Pair("status","arbitrated"));
							break;
						case 'n':
							result.push_back(Pair("status","in settlement"));
							break;
					}
					result.push_back(Pair("last_txid",latesttxid.GetHex()));
				}
				else
					result.push_back(Pair("status","active"));
				GetAgreementUpdateData(latesttxid, name, datahash, disputefee, deposit, revision);
				data.push_back(Pair("revisions",revision));
				if (bHasArbitrator)
					data.push_back(Pair("dispute_fee",(double)disputefee/COIN));
				data.push_back(Pair("contract_name",name));
				data.push_back(Pair("contract_hash",datahash.GetHex()));
				result.push_back(Pair("data",data));
				break;
			case 'u':
				result.push_back(Pair("type","contract update"));
				DecodeAgreementUpdateOpRet(tx.vout[numvouts-1].scriptPubKey, version, agreementtxid, proposaltxid);
				result.push_back(Pair("contract_txid",agreementtxid.GetHex()));
				result.push_back(Pair("proposal_txid",proposaltxid.GetHex()));
				GetAgreementInitialData(agreementtxid, proposaltxid, srcpub, destpub, arbitrator, disputefee, totaldeposit, datahash, dummytxid, name);
				CPK_arbitrator = pubkey2pk(arbitrator);
				GetAgreementUpdateData(txid, name, datahash, disputefee, deposit, revision);
				data.push_back(Pair("revision",revision));
				if (CPK_arbitrator.IsFullyValid())
					data.push_back(Pair("dispute_fee",(double)disputefee/COIN));
				data.push_back(Pair("contract_name",name));
				data.push_back(Pair("contract_hash",datahash.GetHex()));
				result.push_back(Pair("data",data));
				break;
			case 's':
				result.push_back(Pair("type","contract close"));
				DecodeAgreementCloseOpRet(tx.vout[numvouts-1].scriptPubKey, version, agreementtxid, proposaltxid);
				result.push_back(Pair("contract_txid",agreementtxid.GetHex()));
				result.push_back(Pair("proposal_txid",proposaltxid.GetHex()));
				GetAgreementInitialData(agreementtxid, proposaltxid, srcpub, destpub, arbitrator, disputefee, totaldeposit, datahash, dummytxid, name);
				CPK_arbitrator = pubkey2pk(arbitrator);
				GetAgreementUpdateData(txid, name, datahash, disputefee, deposit, revision);
				data.push_back(Pair("revision",revision));
				data.push_back(Pair("contract_name",name));
				data.push_back(Pair("contract_hash",datahash.GetHex()));
				data.push_back(Pair("deposit_for_sender", (double)deposit/COIN));
				data.push_back(Pair("deposit_for_receiver", (double)(totaldeposit-deposit)/COIN));
				data.push_back(Pair("total_deposit", (double)totaldeposit/COIN));
				result.push_back(Pair("data",data));
				break;
			case 'd':
				result.push_back(Pair("type","dispute"));
				DecodeAgreementDisputeOpRet(tx.vout[numvouts-1].scriptPubKey, version, agreementtxid, srcpub, datahash);
				result.push_back(Pair("contract_txid",agreementtxid.GetHex()));
				result.push_back(Pair("source_pubkey",HexStr(srcpub)));
				result.push_back(Pair("data_hash",datahash.GetHex()));
				break;
			case 'r':
				result.push_back(Pair("type","dispute resolution"));
				DecodeAgreementDisputeResolveOpRet(tx.vout[numvouts-1].scriptPubKey, version, agreementtxid, rewardedpubkey);
				result.push_back(Pair("contract_txid",agreementtxid.GetHex()));
				result.push_back(Pair("rewarded_pubkey",HexStr(rewardedpubkey)));
				break;
			case 'n':
				result.push_back(Pair("type","agreement unlock"));
				DecodeAgreementUnlockOpRet(tx.vout[numvouts-1].scriptPubKey, version, agreementtxid, pawnshoptxid);
				result.push_back(Pair("contract_txid",agreementtxid.GetHex()));
				result.push_back(Pair("dest_pawnshop_txid",pawnshoptxid.GetHex()));
				GetAgreementInitialData(agreementtxid, proposaltxid, srcpub, destpub, arbitrator, disputefee, totaldeposit, datahash, dummytxid, name);
				deposit = CheckDepositUnlockCond(pawnshoptxid);
				if (deposit > -1)
				{
					result.push_back(Pair("deposit_sent", (double)deposit/COIN));
					result.push_back(Pair("deposit_refunded", (double)(totaldeposit-deposit)/COIN));
				}
				result.push_back(Pair("total_deposit", (double)totaldeposit/COIN));
				break;
		}
		return(result);
	}
	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "invalid Agreements transaction id");
}
UniValue AgreementUpdateLog(uint256 agreementtxid, int64_t samplenum, bool backwards)
{
    UniValue result(UniValue::VARR);
    int64_t total = 0LL;
	CTransaction agreementtx, latesttx, batontx;
    int32_t numvouts, vini, height, retcode;
    uint256 batontxid, sourcetxid, hashBlock, latesttxid;
    uint8_t funcid;
	if (myGetTransaction(agreementtxid,agreementtx,hashBlock) != 0 && (numvouts = agreementtx.vout.size()) > 0 &&
	(funcid = DecodeAgreementOpRet(agreementtx.vout[numvouts-1].scriptPubKey)) == 'c')
	{
		GetLatestAgreementUpdate(agreementtxid, latesttxid, funcid);
		if (latesttxid != agreementtxid)
		{
			if (backwards)
			{
				total++;
				result.push_back(latesttxid.GetHex());
				myGetTransaction(latesttxid, latesttx, hashBlock);
				batontxid = latesttx.vin[1].prevout.hash;
				while ((total < samplenum || samplenum == 0) && 
				myGetTransaction(batontxid, batontx, hashBlock) && batontx.vout.size() > 0 &&
				(funcid = DecodeAgreementOpRet(batontx.vout[batontx.vout.size() - 1].scriptPubKey)) != 0)
				{
					switch (funcid)
					{
						case 'u':
						case 'd':
							total++;
							result.push_back(batontxid.GetHex());
							batontxid = batontx.vin[1].prevout.hash;
							continue;
						default:
							break;
					}
					break;
				}
			}
			else
			{
				if ((retcode = CCgetspenttxid(batontxid, vini, height, agreementtxid, 1)) == 0 && 
					myGetTransaction(batontxid, batontx, hashBlock) && batontx.vout.size() > 0 &&
					((funcid = DecodeAgreementOpRet(batontx.vout[batontx.vout.size() - 1].scriptPubKey)) == 'u' || funcid == 's' || funcid == 'd'))
				{
					total++;
					result.push_back(batontxid.GetHex());
					sourcetxid = batontxid;
				}
				while ((total < samplenum || samplenum == 0) &&
					(retcode = CCgetspenttxid(batontxid, vini, height, sourcetxid, 0)) == 0 && 
					myGetTransaction(batontxid, batontx, hashBlock) && batontx.vout.size() > 0)
				{
					funcid = DecodeAgreementOpRet(batontx.vout[batontx.vout.size() - 1].scriptPubKey);
					switch (funcid)
					{
						case 'u':
						case 'd':
							total++;
							result.push_back(batontxid.GetHex());
							if (batontxid == latesttxid)
								break;
							else
							{
								sourcetxid = batontxid;
								continue;
							}
						case 'n':
						case 's':
						case 'r':
							result.push_back(batontxid.GetHex());
							break;
						default:
							break;
					}
					break;
				}
			}
		}
		return (result);
	}
	CCERR_RESULT("agreementscc", CCLOG_INFO, stream << "invalid Agreements transaction id");
}
// agreementproposals - returns every unspent proposal that pk is referenced in. agreementtxid can be specified to filter out proposals unrelated to this agreement
UniValue AgreementProposals(CPubKey pk, uint256 agreementtxid)
{
	UniValue result(UniValue::VOBJ), senderlist(UniValue::VARR), receiverlist(UniValue::VARR), arbitratorlist(UniValue::VARR);
	std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressIndexCCMarker;
	std::vector<uint256> foundtxids;
	struct CCcontract_info *cp, C;
	uint256 txid, hashBlock, dummytxid, refagreementtxid;
	std::vector<uint8_t> srcpub, destpub, arbitrator;
	uint8_t version, proposaltype, dummychar;
	int64_t dummyamount;
	std::string dummystr;
	CTransaction vintx;
	cp = CCinit(&C, EVAL_AGREEMENTS);
	CPubKey mypk;
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());
	auto AddProposalWithPk = [&](uint256 txid, uint256 agreementtxid)
	{
		if (myGetTransaction(txid, vintx, hashBlock) != 0 && vintx.vout.size() > 0 && 
		DecodeAgreementProposalOpRet(vintx.vout[vintx.vout.size() - 1].scriptPubKey,version,proposaltype,srcpub,destpub,arbitrator,dummyamount,dummyamount,dummyamount,dummytxid,refagreementtxid,dummytxid,dummystr) == 'p' &&
		!IsProposalSpent(txid, dummytxid, dummychar) &&
		(agreementtxid == zeroid || (proposaltype != 'p' && agreementtxid == refagreementtxid)) &&
		std::find(foundtxids.begin(), foundtxids.end(), txid) == foundtxids.end())
		{
			if (mypk == pubkey2pk(srcpub))
			{
				senderlist.push_back(txid.GetHex());
				foundtxids.push_back(txid);
			}
			if (pubkey2pk(destpub).IsValid() && mypk == pubkey2pk(destpub))
			{
				receiverlist.push_back(txid.GetHex());
				foundtxids.push_back(txid);
			}
			if (pubkey2pk(arbitrator).IsValid() && mypk == pubkey2pk(arbitrator))
			{
				arbitratorlist.push_back(txid.GetHex());
				foundtxids.push_back(txid);
			}
		}
	};
	SetCCunspents(addressIndexCCMarker,cp->unspendableCCaddr,true);
	for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressIndexCCMarker.begin(); it != addressIndexCCMarker.end(); it++)
		AddProposalWithPk(it->first.txhash, agreementtxid);
	result.push_back(Pair("sender",senderlist));
	result.push_back(Pair("receiver",receiverlist));
	result.push_back(Pair("arbitrator",arbitratorlist));
	return (result);
}
// agreementsubcontracts - returns every contract that has agreementtxid defined as its master contract
UniValue AgreementSubcontracts(uint256 agreementtxid)
{
	UniValue result(UniValue::VARR);
	std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressIndexCCMarker;
	std::vector<uint256> foundtxids;
	struct CCcontract_info *cp, C;
	std::vector<uint8_t> dummypk;
	int64_t dummyamount;
	std::string dummystr;
	uint256 txid, hashBlock, dummytxid, refagreementtxid;
	CTransaction vintx;
	cp = CCinit(&C, EVAL_AGREEMENTS);
	auto AddAgreementWithRef = [&](uint256 txid)
	{
		if (myGetTransaction(txid, vintx, hashBlock) != 0 && vintx.vout.size() > 0 && 
		DecodeAgreementOpRet(vintx.vout[vintx.vout.size() - 1].scriptPubKey) == 'c' &&
		GetAgreementInitialData(txid, dummytxid, dummypk, dummypk, dummypk, dummyamount, dummyamount, dummytxid, refagreementtxid, dummystr) &&
		agreementtxid == refagreementtxid &&
		std::find(foundtxids.begin(), foundtxids.end(), txid) == foundtxids.end())
		{
			result.push_back(txid.GetHex());
			foundtxids.push_back(txid);
		}
	};
	SetCCunspents(addressIndexCCMarker,cp->unspendableCCaddr,true);
	for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressIndexCCMarker.begin(); it != addressIndexCCMarker.end(); it++)
		AddAgreementWithRef(it->first.txhash);
	return (result);
}
// agreementinventory - returns every agreement pk is a member of
UniValue AgreementInventory(CPubKey pk)
{
	UniValue result(UniValue::VOBJ), initiatorlist(UniValue::VARR), recipientlist(UniValue::VARR), arbitratorlist(UniValue::VARR);
	std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressIndexCCMarker;
	std::vector<uint256> foundtxids;
	struct CCcontract_info *cp, C;
	uint256 txid, hashBlock, dummytxid;
	std::vector<uint8_t> initiator, recipient, arbitrator;
	int64_t dummyamount;
	std::string dummystr;
	CTransaction vintx;
	cp = CCinit(&C, EVAL_AGREEMENTS);
	auto AddAgreementWithPk = [&](uint256 txid)
	{
		if (myGetTransaction(txid, vintx, hashBlock) != 0 && vintx.vout.size() > 0 && 
		DecodeAgreementOpRet(vintx.vout[vintx.vout.size() - 1].scriptPubKey) == 'c' &&
		GetAgreementInitialData(txid, dummytxid, initiator, recipient, arbitrator, dummyamount, dummyamount, dummytxid, dummytxid, dummystr) &&
		std::find(foundtxids.begin(), foundtxids.end(), txid) == foundtxids.end())
		{
			if (pk == pubkey2pk(initiator))
			{
				initiatorlist.push_back(txid.GetHex());
				foundtxids.push_back(txid);
			}
			if (pk == pubkey2pk(recipient))
			{
				recipientlist.push_back(txid.GetHex());
				foundtxids.push_back(txid);
			}
			if (pk == pubkey2pk(arbitrator))
			{
				arbitratorlist.push_back(txid.GetHex());
				foundtxids.push_back(txid);
			}
		}
	};
	SetCCunspents(addressIndexCCMarker,cp->unspendableCCaddr,true);
	for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressIndexCCMarker.begin(); it != addressIndexCCMarker.end(); it++)
		AddAgreementWithPk(it->first.txhash);
	result.push_back(Pair("initiator",initiatorlist));
	result.push_back(Pair("recipient",recipientlist));
	result.push_back(Pair("arbitrator",arbitratorlist));
	return (result);
}
// agreementsettlements - returns every pawnshop that has been designated to this agreement
// bActiveOnly can be used to filter out inactive/closed pawnshop
UniValue AgreementSettlements(const CPubKey& pk, uint256 agreementtxid, bool bActiveOnly)
{
	UniValue result(UniValue::VARR);
	CPubKey mypk, CPK_initiator, CPK_recipient, dummypk;
	CTransaction agreementtx, tx;
	uint256 txid, hashBlock, refagreementtxid, dummytxid;
	std::vector<uint8_t> initiatorpk, recipientpk, arbitratorpk;
	int32_t numvouts;
	int64_t dummyamount;
	std::string dummystr, pawnshopname;
	uint8_t version, lastfuncid;
	uint32_t pawnshopflags;
	char myCCaddr[65];
	std::vector<uint256> txids;
	struct CCcontract_info *cpPawnshop, CPawnshop;
	cpPawnshop = CCinit(&CPawnshop, EVAL_PAWNSHOP);
	mypk = pk.IsValid() ? pk : pubkey2pk(Mypubkey());
	if (myGetTransaction(agreementtxid,agreementtx,hashBlock) != 0 && agreementtx.vout.size() > 0 &&
	DecodeAgreementOpRet(agreementtx.vout[agreementtx.vout.size()-1].scriptPubKey) == 'c' &&
	GetAgreementInitialData(agreementtxid, dummytxid, initiatorpk, recipientpk, arbitratorpk, dummyamount, dummyamount, dummytxid, dummytxid, dummystr))
	{
		CPK_initiator = pubkey2pk(initiatorpk);
		CPK_recipient = pubkey2pk(recipientpk);
		if (mypk != CPK_initiator && mypk != CPK_recipient)
			return (result);
		GetCCaddress(cpPawnshop,myCCaddr,mypk);
		SetCCtxids(txids,myCCaddr,true,EVAL_PAWNSHOP,CC_MARKER_VALUE,zeroid,'c');
		for (std::vector<uint256>::const_iterator it=txids.begin(); it!=txids.end(); it++)
		{
			txid = *it;
			if (myGetTransaction(txid,tx,hashBlock) != 0 && (numvouts = tx.vout.size()) > 0 &&
			DecodePawnshopCreateOpRet(tx.vout[numvouts-1].scriptPubKey,version,pawnshopname,dummypk,dummypk,pawnshopflags,dummytxid,dummyamount,dummyamount,refagreementtxid) != 0 &&
			refagreementtxid == agreementtxid && GetLatestPawnshopTxid(txid, dummytxid, lastfuncid))
			{
				if (bActiveOnly)
				{
					if (lastfuncid == 'c')
						result.push_back(txid.GetHex());
				}
				else
				{
					result.push_back(txid.GetHex());
				}
			}
		}
	}
	return (result);
}
UniValue AgreementList()
{
	UniValue result(UniValue::VARR);
	std::vector<uint256> foundtxids;
	std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressIndexCCMarker;
	struct CCcontract_info *cp, C; uint256 txid, hashBlock;
	CTransaction vintx;
	cp = CCinit(&C, EVAL_AGREEMENTS);
	auto addAgreementTxid = [&](uint256 txid)
	{
		if (myGetTransaction(txid, vintx, hashBlock) != 0)
		{
			if (vintx.vout.size() > 0 && DecodeAgreementOpRet(vintx.vout[vintx.vout.size() - 1].scriptPubKey) != 0)
			{
				if (std::find(foundtxids.begin(), foundtxids.end(), txid) == foundtxids.end())
				{
					result.push_back(txid.GetHex());
					foundtxids.push_back(txid);
				}
			}
		}
	};
	SetCCunspents(addressIndexCCMarker,cp->unspendableCCaddr,true);
	for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = addressIndexCCMarker.begin(); it != addressIndexCCMarker.end(); it++)
		addAgreementTxid(it->first.txhash);
	return(result);
}*/
