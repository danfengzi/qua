#include "qua_version.h"


/*
 * sym.c:
 *  messy symbol tables
 */


#include "tinyxml2.h"
#include "StdDefs.h"


///////////////// Application headers

#include "Sym.h"
#include "Block.h"
#include "Channel.h"
#include "ControllerBridge.h"
#include "Stackable.h"
#include "QuasiStack.h"
#include "Qua.h"
#include "Template.h"
#include "Pool.h"
#include "Voice.h"
#include "Sample.h"
#include "Clip.h"
#include "Lambda.h"
#include "Envelope.h"
#ifdef QUA_V_VST_HOST
#include "VstPlugin.h"
#endif
#include "Parse.h"
#include "QuaPort.h"

#ifdef QUA_V_APP_HANDLER
#include "include/Application.h"
#endif

SymTab				glob(MAX_SYMBOLS);
StabEnt				*typeSymbolStreamItem=nullptr,
					*typeSymbolClip=nullptr,
					*typeSymbolNote=nullptr,
					*typeSymbolCtrl=nullptr,
					*typeSymbolProg=nullptr,
					*typeSymbolJoy=nullptr,
					*typeSymbolSysCommon=nullptr,
					*typeSymbolBend=nullptr,
					*typeSymbolMsg=nullptr,
					*typeSymbolSysX=nullptr;

flag	debug_symtab=0;
flag	debug_save=1;

StabEnt *
findBuiltin(const string nm, short dc)
{
	StabEnt	*sym;
	if((sym = glob.FindContextSymbol(nm,nullptr)) == nullptr) {
//		reportError("FindBuiltin: %s: not defined in null context\n", nm);
		return nullptr;
    }
    if (sym->type != TypedValue::S_BUILTIN) {
//		reportError("FindBuiltin: %s: not a builtin\n", nm);
		return nullptr;
    }

	return sym;
}



/*
 * FindSong:
 */
Pool *
findPool(const std::string nm, short dc)
{
    StabEnt	*sym;

    if ((sym = glob.FindSymbol(nm, dc)) == nullptr) {
		return nullptr;
    }
    if (sym->type != TypedValue::S_POOL) {
		return nullptr;
    }
    return sym->PoolValue();
}

Sample *
findSample(const std::string  nm, short dc)
{
    StabEnt	*sym;

    if ((sym = glob.FindSymbol(nm, dc)) == nullptr) {
		return nullptr;
    }
    if (sym->type != TypedValue::S_SAMPLE) {
		return nullptr;
    }
    return sym->SampleValue();
}

Voice *
findVoice(std::string  nm, short dc)
{
    StabEnt	*sym;
    if ((sym = glob.FindSymbol(nm, dc)) == nullptr) {
		return nullptr;
    }
    if (sym->type != TypedValue::S_VOICE) {
		return nullptr;
    }
    return sym->VoiceValue();
}


Channel *
findChannel(std::string nm, short dc)
{
    StabEnt	*sym;

    if ((sym = glob.FindSymbol(nm, dc)) == nullptr) {
		return nullptr;
    }
    if (sym->type != TypedValue::S_CHANNEL) {
		return nullptr;
    }
    return sym->ChannelValue();
}

Application *
findApplication(std::string nm, short dc)
{
    StabEnt	*sym;

    if ((sym = glob.FindSymbol(nm, dc)) == nullptr) {
		return nullptr;
    }
    if (sym->type != TypedValue::S_APPLICATION) {
		return nullptr;
    }
    return sym->ApplicationValue();
}


Lambda *
findMethod(const string nm, short dc)
{
    StabEnt	*sym;

    if ((sym = glob.FindSymbol(nm, dc)) == nullptr) {
		return nullptr;
    }
    if (sym->type != TypedValue::S_LAMBDA) {
		return nullptr;
    }
    return sym->LambdaValue();
}


QuaPort *
findQuaPort(const string nm, short dc)
{
	StabEnt		*sym = glob.FindContextSymbol(nm, nullptr, -1);
	if (sym == nullptr) {
		return nullptr;
	}
	if (sym->type != TypedValue::S_PORT) {
		return nullptr;
	}
	return sym->PortValue();
}


Block *
findMethodMain(const string nm, short dc)
{
    StabEnt	*sym;

    if ((sym = glob.FindSymbol(nm, dc)) == nullptr) {
		return nullptr;
    }
    if (sym->type != TypedValue::S_LAMBDA) {
		return nullptr;
    }

    return sym->LambdaValue()->mainBlock;
}

bool
SymTab::ValidSymbolName(char *nm)
{
	if (nm == nullptr) {
		return false;
	}
	if (!isalpha(*nm) && *nm != '_') {
		return false;
	}
	char	*p = nm;
	while (*p) {
		if (!isalnum(*p) && *p != '_') {
			return false;
		}
		p++;
	}
	return true;
}

bool
SymTab::MakeValidSymbolName(char *nm, char *nnm)
{
	if (nm == nullptr || nnm == nullptr) {
		return false;
	}
	while (*nm != '\0' && !isalpha(*nm) && *nm != '_') {
		nm++;
	}
	if (*nm == '\0')
		return false;
	char	*nmp = nnm;
	int		cnt = 0;
	while (*nm != '\0') {
		if (isalnum(*nm) || *nm == '_') {
			*nmp = *nm;
			nmp++;
			if (++cnt >= MAX_QUA_NAME_LENGTH) {
				break;
			}
		}
		nm++;
	}
	*nmp = 0;
	if (*nnm == '\0') {
		return false;
	}
	return true;
}

/*
 * DefineSymbol:
 */
ulong
SymTab::Hash(const std::string nm, StabEnt *context)
{
	ulong	shite=0;
	short	nc = nm.length();

	for (short i = 0; i<nc; i++) {
		shite += nm[i];
    }
	if (debug_symtab >= 2)
		fprintf(stderr, "hash %s %d\n", nm.c_str(), ((ulong) context) % stabLen);
	return (((ulong) context&0xffff) + shite) % stabLen;
}

StabEnt *
SymTab::FindContextSymbol(std::string nm, StabEnt *contxt, short def_cnt)
{
	int	stabind;
	return (stabind = FindContextSymbolInd(nm,contxt,def_cnt)) >= 0?
				stab[stabind]:nullptr;
}

Instance *
FindInstance(StabEnt *ctxt, std::string nm)
{
	if (ctxt == nullptr || nm.size() == 0) {
		return nullptr;
	}
	StabEnt *sym = FindTypedSymbolInCtxt(nm, TypedValue::S_INSTANCE, ctxt);
	return sym->InstanceValue();
}

StabEnt *
FindTypedSymbolInCtxt(std::string nm, base_type_t type, StabEnt *contxt)
{
	StabEnt *sym = glob.FindContextSymbol(nm,contxt,-1);
	if (sym != nullptr && sym->type == type) {
		return sym;
	}
	return nullptr;
}

StabEnt *
FindSymbolInCtxt(std::string nm, StabEnt *contxt)
{
	return glob.FindContextSymbol(nm,contxt,-1);
}


long
SymTab::FindContextSymbolInd(const std::string nm, StabEnt *contxt, short def_cnt)
{
    int		i = Hash(nm, contxt);

    if (1)//debug_symtab >= 2)
    	fprintf(stderr, "symtab: looking for %s in context %s\n", nm.c_str(), contxt?contxt->name:"<glbl>");
    for (;;) {
		if (debug_symtab >= 2) {
   			fprintf(stderr, "stab[%d]: %x\n", i, (unsigned)stab[i]);
   			fprintf(stderr, "\t%s%s context %s\n",
   					stab[i]?stab[i]->name:"(not)",
   					stab[i]&&stab[i]->isDeleted?" (deleted)":"",
   					stab[i]&&stab[i]->context?stab[i]->context->name:"(not)");
   		}
    	if (stab[i] != nullptr && !stab[i]->isDeleted) {
			if (debug_symtab >= 2)
    			fprintf(stderr, "stab %d %x\n", i, (unsigned)stab[i]);
			if ( stab[i]->context == contxt
				&& stab[i]->name == nm
#ifdef DEFINE_COUNT
				&& (def_cnt < 0 || def_cnt == stab[i]->defineCount)
#endif
							) {
					return i;
			}
		} else {
		// need to be careful about deletion, need to keep
		// some kind of marker structure, and keep them. Can't
		// make holes in the sym tab
			return -1;
		}
		i = (i+1) % stabLen;
    }
    return -1;
}

bool
SymTab::Rename(StabEnt *sym, const char *nm)
{
	short	defcnt=-1;
	
	long	oldind = FindContextSymbolInd(sym->name, sym->context,
#ifdef DEFINE_COUNT
								sym->defineCount
#else
								-1
#endif
							);
	long	newind = FindFreeInd(nm, sym->context, defcnt);
	if (newind < 0|| oldind < 0)
		return false;
	stab[newind] = sym;
	delete [] sym->name;
	sym->name = new char[strlen(nm)+1];

#ifdef DEFINE_COUNT
	sym->defineCount = defcnt;
#endif
	
	strcpy(sym->name, nm);
	stab[oldind] = new StabEnt("", TypedValue::S_UNKNOWN, 0, -1, TypedValue::REF_VALUE);
	// need the dummy entry, a null would break the hashing
	return true;
}

long
SymTab::FindSymbolInd(StabEnt *sym, short defcnt)
{
    int		i;
    long	ind;

    if (debug_symtab >= 2)
    	fprintf(stderr, "symtab: looking for %s, context stack length %d\n", sym->name, contextCount);
    for (i=contextCount-1; i>=0; i--) {
		if ((ind=FindContextSymbolInd(sym->name, contextStack[i], defcnt)) >= 0) {
		    return ind;
		}
    }
    if (debug_symtab)
    	fprintf(stderr, "%s not found\n", sym->name, contextCount);
    return -1;
}


SymTab::~SymTab()
{
	if (debug_symtab)
		fprintf(stderr, "deleting symbol table\n");
}

SymTab::SymTab(int n)
{
	stabLen = n;
	contextCount = 0;
	for (short j=0; j<14; j++)
		contextStack[j] = nullptr;
		
	stab = (StabEnt **)malloc(n * (sizeof (StabEnt *)));
	for (long i=0; i<stabLen; i++) {
		stab[i] = nullptr;
	}
}

void
SymTab::DumpGlobals(FILE *fp)
{
	for (int i=0; i<MAX_SYMBOLS; i++) {
		if (stab[i] != nullptr && stab[i]->context == nullptr) {
			stab[i]->Dump(fp, 0);
		}
	}
}

void
SymTab::DumpContexts(FILE *fp)
{
	for (short i=0; i<contextCount; i++) {
		fprintf(fp, "Context %d %s", i, contextStack[i]?contextStack[i]->name:"null ctxt");
	}
}

long
SymTab::FindFreeInd(const char *nm, StabEnt *context, short &defineCount)
{
	int 	i = Hash(nm, context);
	bool	foundDefCount = (defineCount >= 0);
	
	int		spot = -1;

	short	bucketCount = 0;
	if (!foundDefCount) {
		defineCount = 0;
	}
	while (bucketCount < stabLen) {
#ifdef DEFINE_COUNT
		if (  stab[i] != nullptr &&
			  strcmp(nm, stab[i]->name) == 0 &&
			  context == stab[i]->context) {
			if (foundDefCount) {
				if (!stab[i]->isDeleted && stab[i]->defineCount == defineCount) {
					reportError("Qua: symtab defcount clash, needs a fix");
				}
			} else {
				if (stab[i]->isDeleted) { 
					defineCount = stab[i]->defineCount;
					foundDefCount = true;
				} else {
					defineCount = stab[i]->defineCount+1;
				}
			}
		}
#endif
		if (spot < 0 && (stab[i] == nullptr || stab[i]->isDeleted)) {
			if (stab[i]) {
// delete a disused stab ent!
				if (debug_symtab >= 2)
					fprintf(stderr, "Deleting %s\n", stab[i]->UniqueName());
				delete stab[i];
			}
			spot = i;
		}
		
#ifdef DEFINE_COUNT
		if (foundDefCount && spot >= 0)
			break;
#else
		if (spot >= 0)
			break;
#endif			
		bucketCount++;
		i = (i + 1) % stabLen;
	}
	
	if (spot < 0) {
		internalError("Qua: symbol table blowout");
	}
	return spot;
}

StabEnt *
SymTab::AddSymbol(char *nm, base_type_t typ, StabEnt *context,
				int8 nd, ref_type_t refc, short defineCount)
{
	int ind;
	if ((ind = FindFreeInd(nm, context, defineCount)) == B_ERROR){
		return nullptr;
	} else {
		stab[ind] = new StabEnt(nm, typ, context, nd, refc);
#ifdef DEFINE_COUNT
		stab[ind]->defineCount = defineCount;
#endif
		if (debug_symtab >= 2)
			fprintf(stderr, "%s/%d added to glob at %d %x\n", nm, defineCount, ind, (unsigned)stab[ind]);
		return stab[ind];
	}
}

void
SymTab::MoveSymbol(StabEnt *sym, StabEnt *newctxt, StabEnt *symatpos)
{
	if (sym->context) {
		StabEnt	**q = &sym->context->children;
		for (StabEnt *p=sym->context->children; p!=nullptr; p=p->sibling) {
			if (p == sym) {
				*q = sym->sibling;
				break;
			}
			q = &p->sibling;
		}
	}
	sym->context = newctxt;
	if (sym->context) {
		StabEnt	*p, **q = &sym->context->children;
		for (p=sym->context->children; p!=nullptr; p=p->sibling) {
			if (symatpos == p) {
				break;
			}
			q = &p->sibling;
		}
		sym->sibling = p;
		*q = sym;
	}
}

std::string
SymTab::MakeUniqueName(StabEnt *ctxt, std::string basenm, long startind)
{
	short	nmind = startind;
	StabEnt			*fnd = nullptr;
	char nmbuf[100];
	do {
		if (nmind <= 0) {
			strcpy(nmbuf, basenm.c_str());
		} else {
			sprintf(nmbuf, "%s%d", basenm, nmind);
		}
		fnd = FindContextSymbol(nmbuf, ctxt);
		nmind++;
	} while (fnd != nullptr);

	return std::string(nmbuf);
}


void
SymTab::DeleteSymbol(StabEnt *sym, bool doCleanup)
{
//	debug_symtab = 3;
	if (debug_symtab >= 2)
		fprintf(stderr, "SymTab::Delete(%s)\n", sym->name);
	if (sym->isDeleted)
		return;
	sym->isDeleted = true;
	if (debug_symtab >= 2)
		fprintf(stderr, "%s marked as deleted\n", sym->name);
	if (sym->context) {	// remove from sibling list
		StabEnt		**spp = &sym->context->children,
					*sp = sym->context->children;
		for(;sp != nullptr; sp = sp->sibling) {
			if (sp == sym)
				break;
			spp = &sp->sibling;
		}
		if (sp) {
			*spp = sp->sibling;
			if (debug_symtab >= 2)
				fprintf(stderr, "delete %s from context %x\n", sym->name, (unsigned)sym->context);
		}
	}
	
	if (sym->isClone)		// don't free clone symbol or children
		return;
	if (sym->children) {	//delete children
		while (sym->children) {
		// ????? maybe should free the resource, Block etc.
		// and let that do symbol deletion ...
			DeleteSymbol(sym->children, true);
		}
	}
	if (debug_symtab)
		fprintf(stderr, "SymTab::Delete(%s)... clearing object type %d defCnt %d\n", sym->name, sym->type,
#ifdef DEFINE_COUNT
					sym->defineCount
#else
					0
#endif
			);
	switch(sym->type) {
#ifdef QUA_V_APP_HANDLER
	case TypedValue::S_APPLICATION:		delete sym->ApplicationValue(); break;
#endif
	case TypedValue::S_VOICE:			delete sym->VoiceValue(); break;
	case TypedValue::S_LAMBDA:			delete sym->LambdaValue(); break;
	case TypedValue::S_TEMPLATE:		delete sym->TemplateValue(); break;
	case TypedValue::S_POOL:			delete sym->PoolValue(); break;
	case TypedValue::S_SAMPLE:			delete sym->SampleValue(); break;
	case TypedValue::S_TAKE: {
		Take	*t = sym->TakeValue();
		switch (sym->context->type) {
			case TypedValue::S_POOL:
				sym->context->PoolValue()->DeleteTake((StreamTake*)t);
				break;
			case TypedValue::S_VOICE:
				sym->context->VoiceValue()->DeleteTake((StreamTake*)t, true);
				break;
			case TypedValue::S_SAMPLE:
				sym->context->SampleValue()->DeleteTake((SampleTake*)t, true);
				break;
			default: {
				internalError("Failed attempt to delete take \"%s\" from unexpected schedulable \"%s\"", sym->name, sym->context->name);
				break;
			}
		}
		break;
	}
	case TypedValue::S_CLIP: {
// shouldn't need to delete anything, but may need to update some screen objects
		Clip	*c = sym->ClipValue(nullptr);
		if (c) {
			delete c;
		}
		break;
	}
	case TypedValue::S_PORT: {
		switch(sym->PortValue()->deviceType) {
// ???????????????????????????????????????????
// confused here... these deletes should be ok, but aren't 
// !!!!!!!!!!!! must check
//		case QUA_DEV_MIDI: delete (QuaMidiPort *)sym->PortValue(); break;
//		case QUA_DEV_AUDIO: delete (QuaAudioPort *)sym->PortValue(); break;
		}
		break;
	}
		
	case TypedValue::S_INSTANCE: {
		switch (sym->context->type) {
		case TypedValue::S_VOICE: {
			VoiceInstance *p = (VoiceInstance *)sym->InstanceValue();
			delete p;
			break;
		}
		case TypedValue::S_POOL: {
			PoolInstance *p = (PoolInstance *)sym->InstanceValue();
			delete p;
			break;
		}
		case TypedValue::S_SAMPLE: {
			SampleInstance *q = (SampleInstance *)sym->InstanceValue();
			delete q;
			break;
		}
		default:
			delete sym->InstanceValue();
			break;
		}
		break;
	}
//	case TypedValue::S_CHANNEL:			delete sym->ChannelValue(); break;
// deleted as screen obj ...
	}
	if (debug_symtab >= 3) {
		fprintf(stderr, "delete done!\n");
	}
}

void
SymTab::PushContext(StabEnt *contxt)
{
    if (contextCount < MAX_CONTEXT_STACK ) {
		contextStack[contextCount] = contxt;
		contextCount++;
    } else {
		internalError("PushContext symtab crash ! \n");
    }
}

StabEnt *
SymTab::TopContext()
{
	if (contextCount == 0) {
		return nullptr;
    } else if (contextCount < MAX_CONTEXT_STACK ) {
		return contextStack[contextCount-1];
    } else {
		fprintf(stderr, "SymTab::TopContext() stack popped below 0... urgh\n");
    }
    return nullptr;
}

StabEnt *
SymTab::PopContext(StabEnt *sym)
{
//	reportError("popping %s cnt %d", sym?sym->name:"top sym", contextCount);
	if (contextCount > 0) {
		StabEnt	*ret = TopContext();
		contextCount--;
		return ret;
	} else {
		fprintf(stderr, "SymTab::PopContext() attempts to pop below stack... urgh\n");
		return nullptr;
    }
}

StabEnt *
SymTab::FindSymbol(std::string nm, short def_cnt)
{
    int		i;
    StabEnt	*p;

	char	trueNm[MAX_QUA_NAME_LENGTH];
	char buf[MAX_QUA_NAME_LENGTH];
	strcpy(buf, nm.c_str());
	char	*cp = buf, *np = trueNm;
	while (*cp && *cp != '#') { *np++ = *cp++; }
	*np = 0;
	if (*cp == '#') {
		def_cnt = atoi(cp+1);
	}
	
    if (debug_symtab >= 2)
		fprintf(stderr, "symtab: looking for %s, def %d count %d\n", trueNm, def_cnt, contextCount);
    for (i=contextCount-1; i>=0; i--) {
		if ((p=FindContextSymbol(trueNm, contextStack[i], def_cnt)) != nullptr) {
		    return p;
		}
    }
    return FindContextSymbol(trueNm, nullptr, def_cnt);
}



///////////////////////////////////////////////////////////
// StabEnt::*
///////////////////////////////////////////////////////////

StabEnt::StabEnt(char *nm, base_type_t typ, StabEnt *ctext, int8 nd, ref_type_t reft)
{
    name = new char[strlen(nm)+1];
    strcpy(name, nm);
    Set(typ, reft, context, 0);
    if (nd >= 0) {
	    size = new q_size_t[nd+1];
    } else {
    	size = nullptr;
    }
    context = ctext;
    isDeleted = false;
#ifdef DEFINE_COUNT
    defineCount = 0;
#endif
    children = nullptr;
    sibling = nullptr;
	isClone = false;
	isEnveloped = false;
    hasBounds = false;
    hasInit = false;
    isHidden = false;
    val.pointer = nullptr;
    SetDefaultBounds();
}

void
StabEnt::SetDefaultBounds()
{
    switch (type) {
    case S_BOOL: {
	    minVal.Set(S_BOOL, REF_VALUE); minVal.SetValue((int32)0);
	    maxVal.Set(S_BOOL, REF_VALUE); maxVal.SetValue((int32)1);
	    iniVal.Set(S_BOOL, REF_VALUE); iniVal.SetValue((int32)0);
    	break;
    }
    case S_BYTE: {
	    minVal.Set(S_BYTE, REF_VALUE); minVal.SetValue((int32)-127);
	    iniVal.Set(S_BYTE, REF_VALUE); iniVal.SetValue((int32)0);
	    maxVal.Set(S_BYTE, REF_VALUE); maxVal.SetValue((int32)127);
    	break;
    }
    case S_SHORT: {
	    minVal.Set(S_SHORT, REF_VALUE); minVal.SetValue((int32)-32767);
	    iniVal.Set(S_SHORT, REF_VALUE); iniVal.SetValue((int32)0);
	    maxVal.Set(S_SHORT, REF_VALUE); maxVal.SetValue((int32)32767);
    	break;
    }
    case S_INT: {
	    minVal.Set(S_INT, REF_VALUE); minVal.SetValue((int32)-2147483647);
	    iniVal.Set(S_INT, REF_VALUE); iniVal.SetValue((int32)0);
	    maxVal.Set(S_INT, REF_VALUE); maxVal.SetValue((int32)2147483647);
    	break;
    }
    case S_LONG: {
	    minVal.Set(S_LONG, REF_VALUE); minVal.SetValue((int64)-2147483647);
	    iniVal.Set(S_LONG, REF_VALUE); iniVal.SetValue((int64)0);
	    maxVal.Set(S_LONG, REF_VALUE); maxVal.SetValue((int64)2147483647);
    	break;
    }
    case S_FLOAT: {
	    minVal.Set(S_FLOAT, REF_VALUE); minVal.SetValue((float)0);
	    iniVal.Set(S_FLOAT, REF_VALUE); iniVal.SetValue((float)0);
	    maxVal.Set(S_FLOAT, REF_VALUE); maxVal.SetValue((float)1);
    	break;
    }
    case S_TIME: {
	    minVal.Set(S_TIME, REF_VALUE); minVal.Set(0, &Metric::std);
	    iniVal.Set(S_TIME, REF_VALUE); iniVal.Set(0, &Metric::std);
	    maxVal.Set(S_TIME, REF_VALUE); maxVal.Set(0x7ffffff, &Metric::std);
    	break;
    }
	case S_TAKE: {
		iniVal = TypedValue::Pointer(S_TAKE, 0);
    	break;
    }
	case S_CLIP: {
		iniVal = TypedValue::Pointer(S_CLIP, 0);
    	break;
    }
   	case S_PORTPARAM: {
	    minVal.Set(S_FLOAT, REF_VALUE); minVal.SetValue((float)0);
	    iniVal.Set(S_FLOAT, REF_VALUE); iniVal.SetValue((float)0.7);
	    maxVal.Set(S_FLOAT, REF_VALUE); maxVal.SetValue((float)1);
   		break;
   	}
   	
    default:
	    minVal.Set(S_UNKNOWN, REF_VALUE); minVal.SetValue((void *)0);
	    maxVal.Set(S_UNKNOWN, REF_VALUE); maxVal.SetValue((void *)1);
	    iniVal.Set(S_UNKNOWN, REF_VALUE); iniVal.SetValue((void *)0);
	}
}

StabEnt::~StabEnt()
{
	if (name)
		delete [] name;
	if (size)
		delete [] size;
}


bool
StabEnt::RefersToStream()
{
	for (StabEnt *p=context; p; p=p->context) {
		switch (p->type) {
		case S_STREAM_ITEM:
			return true;
		case S_VOICE:
		case S_POOL:
		case S_LAMBDA:
		case S_APPLICATION:
		case S_SAMPLE:
		case S_QUA:
			return false;
		}
	}
	return false;
}



// handy for hacks but a slugs way of doing it
Qua *
StabEnt::TopContext()
{
	StabEnt *p;
	for (p = context; p ; p = p->context)
		if (p->type == S_QUA)
			return p->val.qua;
	return nullptr;
}


void
StabEnt::SetInit(TypedValue ini)
{
	hasInit = true;
	iniVal = ini;
}

bool
StabEnt::SetDisplayMode(ulong displayMode)
{
	controlMode = displayMode;
	return true;
}

bool
StabEnt::SetEnvelopeMode(bool enable)
{
	return true;
}

bool
StabEnt::SetVisualParameters(Block *b)
{
	/*
	switch (type) {
		case S_VOICE:	return val.voice->interfaceBridge.SetDisplayInfo(b);
		case S_POOL:	return val.pool->interfaceBridge.SetDisplayInfo(b);
		case S_LAMBDA:	return val.lambda->interfaceBridge.SetDisplayInfo(b);
		case S_TEMPLATE:	return val.qTemplate->interfaceBridge.SetDisplayInfo(b);
		case S_SAMPLE:	return val.sample->interfaceBridge.SetDisplayInfo(b);
#ifdef Q_APP_HANDLER
		case S_APPLICATION:	return val.application->interfaceBridge.SetDisplayInfo(b);
#endif

	}
	*/
	return true;
}

void
StabEnt::SetBounds(TypedValue min, TypedValue max)
{
	hasBounds = true;
	minVal = min;
	maxVal = max;
	if (debug_symtab) {
		fprintf(stderr, "StabEnt::SetBounds(%s, ", min.StringValue());
		fprintf(stderr, "%s) by val\n", max.StringValue());
	}
}

void
StabEnt::SetBounds(TypedValue min, TypedValue ini, TypedValue max)
{
	SetBounds(min, max);
	SetInit(ini);
}

void
StabEnt::SetBounds(int min, int max)
{
	hasBounds = true;
	minVal = TypedValue::Int(min);
	maxVal = TypedValue::Int(max);
	if (debug_symtab) {
		fprintf(stderr, "StabEnt::SetBounds(%d, %d) by int\n", min, max);

	}
}

void
StabEnt::SetHidden(bool hd)
{
	isHidden = hd;
}

bool
StabEnt::IsControlVar()
{
	return controlMode != StabEnt::DISPLAY_NOT;
}


char *
StabEnt::PreciseName()
{
	static char	buf[1024];
	StabEnt	*stk[32];
	short	nstk=0;

	StabEnt	*c = context;
	while (c && nstk < 32) {
		switch (c->type) {
		case S_PORT:
		case S_SAMPLE:
		case S_QUA:
		case S_CHANNEL:
		case S_VOICE:
		case S_POOL:
		case S_APPLICATION:
			goto dump;
		}
		stk[nstk++] = c;
		c = c->context;
	}
dump:
	short pos=0;
	while (nstk > 0) {
		pos += sprintf(buf+pos, "%s.", stk[--nstk]->name);
	}
#ifdef DEFINE_COUNT
	if (defineCount == 0) {
		sprintf(buf+pos, "%s", name);
	} else {
		sprintf(buf+pos, "%s#%d", name, defineCount);
	}
#else
	sprintf(buf+pos, "%s", name);
#endif
	return buf;
}

char *
StabEnt::UniqueName()
{
	static char	buf[MAX_QUA_NAME_LENGTH+3];
#ifdef DEFINE_COUNT
	if (defineCount == 0) {
		return name;
	} else {
		sprintf(buf, "%s#%d", name, defineCount);
		return buf;
	}
#else
	return name;
#endif
}

char *
StabEnt::PrintableName()
{
	static char		nm[MAX_QUA_NAME_LENGTH];
	char	*p = nm;
	char 	*s = name;
	while (*s) {
		switch (*s) {
		case ' ': *p++ = '\\'; *p++=' '; break;
		case '\t': *p++ = '\\'; *p++='t'; break;
		case '.': *p++ = '\\'; *p++='.'; break;
		default:
			*p++ = *s;
		}
		s++;
	}
#ifdef DEFINE_COUNT
	if (defineCount == 0) {
		*p = '\0';
	} else {
		sprintf(p, "\\%d", defineCount);
	}
#else
	*p = '\0';
#endif
	return nm;
}

status_t
StabEnt::SaveSnapshot(FILE *fp)
{
	status_t	err=B_NO_ERROR;
	if (debug_save)
		fprintf(stderr, "save %s\n", name);	

	if (!isDeleted && !isClone) {
		switch(type) {
			case S_QUA: {
				fprintf(fp, "<qua name=\"%s\">\n", name);
				// save global values

				// save display data

				// save children ... channels and schedulables
				StabEnt	*p = children;
				while (p!= nullptr) {
					err=p->SaveSnapshot(fp);
					if (err != B_NO_ERROR) {
						break;
					}
					p = p->sibling;
				}
				p = children;
				while (p!= nullptr) {
					switch (p->type) {
						case S_BOOL:
						case S_SHORT:
						case S_BYTE:
						case S_INT:
						case S_TIME:
						case S_FLOAT:
						case S_LONG:
						case S_DOUBLE: {
							if (p->refType == REF_POINTER || p->refType == REF_VALUE) {
								err=p->SaveSimpleTypeSnapshot(fp, nullptr, nullptr);
							}
							break;
						}
					}
					p = p->sibling;
				}
				fprintf(fp, "</qua>\n");
				break;
			}
				
			case S_TEMPLATE:
				// no snapshot data?
				break;
				
			case S_VOICE: {
				fprintf(fp, "<voice name=\"%s\">\n", name);
				// save stream take data
				// save display data
				// save instance stacks
				StabEnt	*p = children;
				while (p!= nullptr) {
					err=p->SaveSnapshot(fp);
					if (err != B_NO_ERROR) {
						break;
					}
					p = p->sibling;
				}
				fprintf(fp, "</voice>\n");
				break;
			}
				
			case S_SAMPLE: {
				fprintf(fp, "<sample name=\"%s\">\n", name);
				// save sample take data
				// save display data
				// save instance stacks
				StabEnt	*p = children;
				while (p!= nullptr) {
					err=p->SaveSnapshot(fp);
					if (err != B_NO_ERROR) {
						break;
					}
					p = p->sibling;
				}
				fprintf(fp, "</sample>\n");
				break;
			}
				
			case S_POOL: {
				fprintf(fp, "<pool name=\"%s\">\n", name);
				// save stream data
				// save display data
				// save instance stacks
				StabEnt	*p = children;
				while (p!= nullptr) {
					err=p->SaveSnapshot(fp);
					if (err != B_NO_ERROR) {
						break;
					}
					p = p->sibling;
				}
				fprintf(fp, "</pool>\n");
				break;
			}
				
#ifdef QUA_V_APP_HANDLER
			case S_APPLICATION: {
				// save stream data
				// save display data
				// save instance stacks
				fprintf(fp, "<application name=\"%s\">\n", name);
				StabEnt	*p = children;
				while (p!= nullptr) {
					err=p->SaveSnapshot(fp);
					if (err != B_NO_ERROR) {
						break;
					}
					p = p->sibling;
				}
				fprintf(fp, "</application>\n");
				break;
			}
#endif
				
			case S_PORT:
				break;
				
			case S_LAMBDA:
				break;

			case S_INPUT: {
				Input	*in = InputValue();
				Channel	*ch = context->ChannelValue();
				if (in && ch) {
					in->SaveSnapshot(fp, ch);
				}
				break;
			}
				
			case S_OUTPUT: {
				Output	*out = OutputValue();
				Channel	*ch = context->ChannelValue();
				if (out && ch) {
					out->SaveSnapshot(fp, ch);
				}
				break;
			}
				
			case S_CHANNEL: {
				Channel	*chan = ChannelValue();
				if (chan) {
					chan->SaveSnapshot(fp);
				}
				break;
			}
				
			case S_INSTANCE: {
				Instance	*inst = InstanceValue();
				if (inst) {
					inst->SaveSnapshot(fp);
				}
				break;
			}
				
			case S_STREAM: {
				break;
			}
				
			case S_TAKE: {
				Take	* take = TakeValue();
				if (take) {
					take->SaveSnapshot(fp);
				}
				break;
			}
				
			case S_CLIP: {
				Clip	* clip = ClipValue(nullptr);
				if (clip) {
					clip->SaveSnapshot(fp);
				}
				break;
			}
				
			case S_EVENT:
				break;
				
			case S_LONG	:
			case S_SHORT:
			case S_BOOL	:
			case S_FLOAT:
			case S_DOUBLE:
			case S_INT:
			case S_BYTE: {
				break;
			}
				
			// internal or user probably should be saved
			case S_TIME:
			case S_STRING:
			case S_LIST:
			case S_EXPRESSION:
			case S_BLOCK:
			case S_MESSAGE:
				break;

			// internal or user probably should be saved
			case S_NOTE	:
			case S_CTRL:
			case S_SYSX	:
			case S_SYSC	:
			case S_BEND:
			case S_PROG:
			case S_JOY:
				break;

			// internal probably not saved
			case S_VALUE:
			case S_STREAM_ITEM:
				break;

			case S_UNKNOWN:
			default:
				break;
		}
	}
	return B_NO_ERROR;
}


status_t
StabEnt::SaveInitialAssigns(FILE *fp, short indent, Stacker *stacker, QuasiStack *stack)
{
	status_t	err=B_NO_ERROR;
	if (debug_save)
		fprintf(stderr, "save %s\n", name);	

	if (!isDeleted && !isClone) {
		switch(type) {
			case S_LONG	:
			case S_SHORT:
			case S_BOOL	:
			case S_FLOAT:
			case S_INT:
			case S_BYTE:
				if ((err=SaveSimpleTypeInitialAssigns(fp, indent, stacker, stack)) != B_NO_ERROR) {
					return err;
				}
				fprintf(fp, "\n");
				break;
				
		}
	}
	if (sibling) {
		if ((err=sibling->SaveInitialAssigns(fp, indent, stacker, stack)) != B_NO_ERROR) {
			return err;
		}
	}
	return err;
}


status_t
StabEnt::SaveSimpleTypeSnapshot(FILE *fp, Stacker *stacker, QuasiStack *qs, char *additionalAttributes)
{
	LValue	lv;
	SetLValue(lv, nullptr, stacker, nullptr, qs);
	ResultValue	rv = lv.CurrentValue();
	if (isEnveloped) {
		if (additionalAttributes != nullptr) {
			fprintf(fp, "<envelope name=\"%s\" value=\"%s\" %s>\n", name, rv.StringValue(), additionalAttributes);
		}
		else {
			fprintf(fp, "<envelope name=\"%s\" value=\"%s\">\n", name, rv.StringValue());
		}
		fprintf(fp, "</envelope>\n");
	}
	else {
		if (additionalAttributes != nullptr) {
			fprintf(fp, "<fixed name=\"%s\" value=\"%s\" %s/>\n", name, rv.StringValue(), additionalAttributes);
		}
		else {
			fprintf(fp, "<fixed name=\"%s\" value=\"%s\"/>\n", name, rv.StringValue());
		}
	}
	return B_OK;
}


status_t
StabEnt::SaveSimpleTypeInitialAssigns(FILE *fp, short indent, Stacker *stacker, QuasiStack *stack)
{
	char	*strval = StringValue(NULL, stacker, stack);
	if (strval && *strval) {
		tab(fp, indent);
		fprintf(fp, "%s = %s", PrintableName(), strval);
	}
	return B_NO_ERROR;
}

status_t
StabEnt::SaveScript(FILE *fp, short indent, bool saveControllers, bool saveInternalRefs)
{
	status_t	err=B_NO_ERROR;
	if (debug_save)
		fprintf(stderr, "save %s\n", name);	

	if (!isDeleted && !isClone
			&& (saveInternalRefs || (refType != REF_INSTANCE && refType != REF_POINTER))
			/*&& !controlInfo*/
			/* && refType != REF_POINTER */
				) {
		switch(type) {
			case S_QUA:
				if ((err=QuaValue()->Save(fp, indent, true)) != B_NO_ERROR) {
					return err;
				}
				break;
				
			case S_TEMPLATE:
				if ((err=TemplateValue()->save(fp, indent)) != B_NO_ERROR) {
					return err;
				}
				break;
				
			case S_VOICE:
				if ((err=VoiceValue()->Save(fp, indent)) != B_NO_ERROR) {
					return err;
				}
				break;
				
			case S_SAMPLE:
				if ((err=SampleValue()->Save(fp, indent)) != B_NO_ERROR) {
					return err;
				}
				break;
				
			case S_POOL:
				if ((err=PoolValue()->Save(fp, indent)) != B_NO_ERROR) {
					return err;
				}
				break;
				
#ifdef QUA_V_APP_HANDLER
			case S_APPLICATION:
				if ((err=ApplicationValue()->Save(fp, indent)) != B_NO_ERROR) {
					return err;
				}
				break;
#endif
				
			case S_PORT:
				if ((err=PortValue()->save(fp, indent)) != B_NO_ERROR) {
					return err;
				}
				break;
				
			case S_LAMBDA:
				if ((err=LambdaValue()->Save(fp, indent)) != B_NO_ERROR) {
					return err;
				}
				break;
				
			case S_CHANNEL:
				if ((err=ChannelValue()->Save(fp, indent)) != B_NO_ERROR) {
					return err;
				}
				break;

			case S_INSTANCE: {
				tab(fp, indent);
				Instance	*inst = InstanceValue();
				if (inst) {
					fprintf(fp, "instance %s(", inst->sym->name);
					fprintf(fp, "%s,", inst->channel->sym->name);
					fprintf(fp, "%s,", inst->startTime.StringValue());
					fprintf(fp, "%s)\n", inst->duration.StringValue());
	//				if ((err=InstanceValue()->Save(fp, indent)) != B_NO_ERROR) {
	//					return err;
	//				}
				}
				break;
			}

			case S_CLIP: {
				Clip	*c = ClipValue(nullptr);
				if (c && c->sym && c->sym->name) {
					if (c->media && c->media->sym && c->media->sym->name) {
						tab(fp, indent);
						fprintf(fp, "clip %s(", c->sym->name);
						fprintf(fp, "%s,", c->media->sym->name);
						fprintf(fp, "%s,", c->start.StringValue());
						fprintf(fp, "%s)\n", c->duration.StringValue());
					}
				}
				break;
			 }
				
			case S_TAKE: {
				Take	*t = TakeValue();
				if (t) {
					if (t->type == Take::SAMPLE) {
						SampleTake *st = (SampleTake*)t;
						tab(fp, indent);
						fprintf(fp, "take \\sample \"%s\" %s\n", st->path.c_str(), t->sym->name);
					} else if (t->type == Take::STREAM) {
						StreamTake *st = (StreamTake*)t;
						tab(fp, indent);
						fprintf(fp, "take \\stream %s\n", t->sym->name);
					}
				}
				break;
			 }
				
			case S_STREAM:
				break;
				
			case S_EVENT:
				break;
				
			case S_LONG	:
			case S_SHORT:
			case S_BOOL	:
			case S_FLOAT:
			case S_INT:
			case S_BYTE:
				if ((err=SaveSimpleType(fp, indent, saveInternalRefs)) != B_NO_ERROR) {
					return err;
				}
				break;
				
			// internal or user probably should be saved
			case S_TIME:
			case S_STRING:
			case S_LIST:
			case S_NOTE	:
			case S_CTRL:
			case S_SYSX	:
			case S_SYSC	:
			case S_BEND:
			case S_EXPRESSION:
			case S_BLOCK:
			case S_MESSAGE:
				break;

			// internal probably not saved
			case S_VALUE:
			case S_STREAM_ITEM:
				break;

			case S_UNKNOWN:
			default:
				break;
		}
	}
	if (debug_save)
		fprintf(stderr, "done %d: sibl %x\n", indent, (unsigned)sibling);
	if (sibling) {
		if ((err=sibling->SaveScript(fp, indent, saveControllers, saveInternalRefs)) != B_NO_ERROR)
			return err;
	}
	return B_NO_ERROR;
}

status_t
StabEnt::SaveValue(FILE *fp, Stacker *i, QuasiStack *s)
{
	status_t	err=B_NO_ERROR;
	fprintf(fp, StringValue(nullptr, i, s));
	return B_NO_ERROR;
}

char *
StabEnt::StringValue(StreamItem *items, Stacker *stacker, QuasiStack *stack)
{
	static char	buf[10*1024];

	LValue		lval;
	SetLValue(lval, items, stacker, nullptr, stack);
	switch (type) {
	case S_BOOL:
		return ((char *) (*((bool*)lval.addr)?"true":"false"));
	case S_BYTE:
		sprintf(buf, "%d", *((char*)lval.addr));
		return buf;
	case S_SHORT:
		sprintf(buf, "%d", *((int16*)lval.addr));
		return buf;
	case S_INT:
		sprintf(buf, "%d", *((int32*)lval.addr));
		return buf;
	case S_LONG:
		sprintf(buf, "%lld", *((int64*)lval.addr));
		return buf;
	case S_FLOAT:
		sprintf(buf, "%g", *((float*)lval.addr));
		return buf;
	case S_STRING:
		return val.string;
	case S_QUA:
		return val.qua->sym->name;
	case S_TEMPLATE:
		return val.qTemplate->sym->name;
	case S_VOICE:
		return val.voice->sym->name;
	case S_SAMPLE:
		return val.sample->sym->name;
	case S_POOL:
		return val.pool->sym->name;

#ifdef QUA_V_APP_HANDLER
	case S_APPLICATION:
		return val.application->sym->name;
#endif

	case S_LAMBDA:
		return val.lambda->sym->name;
	case S_INSTANCE:
		return val.instance->sym->name;
	case S_EXPRESSION:
	case S_BLOCK: {
		long	len = 0;
		Block	*block = *((Block **)lval.addr);
		
		if (!block) {
			return "";
		} else if (!(Esrap(block, buf, len, 10*1024, false, 0, 1))) {
			internalError("block size too large...");
			return "";
		} else {
			return buf;
		}
		break;
	}
	case S_TIME:
		if (((Time *)lval.addr)->metric == &Metric::std) {
			sprintf(buf, "%s",
					((Time*)lval.addr)->StringValue());
		} else {
			sprintf(buf, "%s'%s",
					((Time*)lval.addr)->StringValue(),
					((Time*)lval.addr)->metric->name);
		}
		return buf;
	default:
		internalError("string value of unexpected type %d", type);
		sprintf(buf, "", type);
		return buf;
	}
	return "!";
}


status_t
StabEnt::SaveSimpleType(FILE *fp, short indent, bool saveInternalRefs)
{

	if (!saveInternalRefs && (refType == REF_INSTANCE || refType == REF_POINTER)) {
		return B_NO_ERROR;
	}

	tab(fp, indent);
//	fprintf(fp, "define ");
	SaveSimpleBits(fp, indent);
	fprintf(fp, "\n");

	return B_NO_ERROR;
}


status_t
StabEnt::SaveSimpleBits(FILE *fp, short indent)
{
	fprintf(fp, "%s", qut::unfind(typeIndex, (int)type).c_str());
	if (hasBounds) {
		fprintf(fp, " \\range %s", minVal.StringValue());
		fprintf(fp, " %s",  maxVal.StringValue());
	}
	fprintf(fp, " %s", PrintableName());
	if (hasInit) {
		fprintf(fp, " = %s",  iniVal.StringValue());
	}
	return B_NO_ERROR;
}



void
StabEnt::Dump(FILE *fp, short ind)
{
	for (short i=0; i<ind; i++)
		fprintf(fp, "\t");
	const char * k = qut::unfind(typeIndex, (int) type).c_str();
	if (k) {
		fprintf(fp, "%s (%s)", name, k);
	} else {
		fprintf(fp, "%s (type %d)", name, type);
	}
#ifdef DEFINE_COUNT
	if (defineCount > 1) {
		fprintf(fp, " (defines = %d)",  defineCount);
	}
#endif
	if (hasInit) {
		fprintf(fp, " (init = %s)",  iniVal.StringValue());
	}
	if (hasBounds) {
		fprintf(fp, " (range [%s,",  minVal.StringValue());
		fprintf(fp, "%s])",  maxVal.StringValue());
	}
	fprintf(fp, "\n");
	for (StabEnt *p=children; p; p=p->sibling) {
		p->Dump(fp, ind+1);
	}
}

// length for allocqation in bytes
int
StabEnt::AllocationLength()
{
	long	bl=1;
	if (!size)
		return 0;
	for (short i=0; i<=indirection; i++) {
		bl *= size[i];
	}
	
	return bl;
}

StabEnt *
StabEnt::TypeSymbol()
{
	switch(type) {
	case S_TEMPLATE:
	case S_QUA:
	case S_VOICE:
	case S_POOL:
	case S_SAMPLE:
	case S_APPLICATION:
	case S_LAMBDA:
	case S_INSTANCE:
	case S_STREAM:
	case S_CHANNEL:
	case S_STRUCT:
	case S_VST_PLUGIN:
		return this;

	case S_UNKNOWN:
	case S_VALUE:
	case S_EXPRESSION:
	case S_BLOCK:
	case S_TIME:
	case S_FLOAT:
	case S_LONG:
	case S_INT:
	case S_SHORT:
	case S_BYTE:
	case S_BOOL:
	case S_STRING:
	case S_LIST:
	case S_LOG_ENTRY:
	case S_STRANGE_POINTER:
	case S_FILE:
	case S_PORT:
	case S_PORTPARAM:
	case S_PARAMGROUP:
	case S_EVENT:
	case S_LIST_ITEM:
		return nullptr;
	
	
	case S_INPUT:
		if (((Input *)val.pointer)->device) {
			return ((Input *)val.pointer)->device->sym;
		}
		return nullptr;
	case S_OUTPUT:
		if (((Output *)val.pointer)->device) {
			return ((Input *)val.pointer)->device->sym;
		}
		return nullptr;

	case S_STREAM_ITEM:
		return typeSymbolStreamItem;
	case S_NOTE:
		return typeSymbolNote;
	case S_CTRL:
		return typeSymbolCtrl;
	case S_SYSX:
		return typeSymbolSysX;
	case S_SYSC:
		return typeSymbolSysCommon;
	case S_MESSAGE:
		return typeSymbolMsg;
	case S_BEND:
		return typeSymbolBend;
	case S_JOY:
		return typeSymbolJoy;
	case S_PROG:
		return typeSymbolProg;
	case S_CLIP:
		return typeSymbolClip;
	}

	return nullptr;
}

status_t
StabEnt::SetAtomicSnapshot(tinyxml2::XMLElement *element, Stacker *stacker, StabEnt *stackerSym, QuasiStack *stack)
{
	const char *valAttrVal = element->Attribute("value");
	const char *positionVal = element->Attribute("position");
	const char *nameAttrVal = element->Attribute("name");
	const char *typeAttrVal = element->Attribute("type");
	const char *encodingAttrVal = element->Attribute("encoding");

	std::string valAttr;
	std::string	nameAttr;
	std::string	typeAttr;

	bool	hasNameAttr = false;
	bool	hasScriptAttr = false;
	bool	hasTypeAttr = false;

	int		encoding = 0;
	int		position = 0;

	if (valAttrVal != nullptr) {
		valAttr = valAttrVal;
	}
	if (positionVal != nullptr) {
		position = atoi(valAttrVal);
	}
	if (nameAttrVal != nullptr) {
		nameAttr = nameAttrVal;
		hasNameAttr = true;
	}
	if (typeAttrVal != nullptr) {
		typeAttr = typeAttrVal;
		hasTypeAttr = true;
	}
	if (encodingAttrVal != nullptr) {
		if (std::string(encodingAttrVal) == "base64") {
			encoding = 1;
		}
	}

	std::string namestr = element->Value();


	if (namestr == "fixed") {
		if (valAttr.size() > 0) {
			if (type == S_VST_PARAM && position >= 0) {
// avoid some potential ambiguiities with vst param names 
// as qua symbols, and issues with params changing according to
// program being set
#ifdef QUA_V_VST_HOST
				if (stack->stk.afx) {
					double val = atof(valAttr.c_str());
					stack->stk.afx->setParameter(stack->stk.afx, position, val);
				}
#endif
			} else {
				LValue	lval;
				SetLValue(lval, nullptr, stacker, stackerSym,stack);
				lval.SetToString(valAttr.c_str());
			}
		}
	} else if (namestr == "envelope") {
	} else {
	}

	return B_OK;
}


//////////////////////////////////////////////////////////
// DefineSymbol, DupSymbol
//////////////////////////////////////////////////////////

StabEnt *
DupSymbol(StabEnt *model, StabEnt *context)
{
	StabEnt *sym;
	if (model->type == TypedValue::S_LAMBDA) {
		Lambda *so =new Lambda(model->LambdaValue(), context);
		sym = so->sym;
	} else {
		sym = DefineSymbol(model->name,
						model->type,
						model->indirection,
						model->val.pointer,
						context,
						model->refType,
						model->isClone,
						model->isEnveloped,
						model->controlMode);
		if (model->refType == TypedValue::REF_STACK || model->refType == TypedValue::REF_INSTANCE) {
			sym->val.stackAddress.offset = model->val.stackAddress.offset;
			sym->val.stackAddress.context = context;
		} else {
			sym->Set(model);
		}
		sym->hasBounds = model->hasBounds;
		sym->hasInit = model->hasInit;
		sym->minVal = model->minVal;
		sym->iniVal = model->iniVal;
		sym->maxVal = model->maxVal;
	}

	StabEnt		*p = model->children, *q, **qq = &sym->children;
	
	while (p != nullptr) {
		q = DupSymbol(p, sym);
		p = p->sibling;
	}
// ????????? reverses order ......
	return sym;
}

StabEnt *
DefineSymbol(std::string nm, base_type_t typ, int8 ndim,
			void *val, StabEnt *context,
			ref_type_t refType, bool isClone, bool isEnveloped, short ctlMode)
{
	short defineCount;
	char	name[MAX_QUA_NAME_LENGTH];
	strcpy(name, nm.c_str());
	char	*p = name;
	while (*p && *p != '#') p++;
	if (*p == '#') {
		defineCount = atoi(p+1);
		if (defineCount < 0)
			defineCount = 0;
		*p = 0;
	} else {
		defineCount = -1;
	}
	StabEnt *sym = glob.AddSymbol(name, typ, context, ndim, refType, defineCount);

	if (sym == nullptr) {
		if (defineCount >= 0)
			internalError("Can't define %s#%d", nm, defineCount);
		else
			internalError("Can't define %s", nm);
		return nullptr;
	}
    if (debug_symtab)
		fprintf(stderr, "define %s/%d type %d val %x ctxt %s %d: %s %s:",
				nm.c_str(),
#ifdef DEFINE_COUNT
				sym->defineCount,
#else
				0,
#endif
				typ, (unsigned)val,
				context?context->name:"<glbl>",
				context?context->type:-1,
				isClone?"clone":"", 
				(ctlMode != StabEnt::DISPLAY_NOT)?"controller":"");

	if (refType >= TypedValue::REF_STACK) {
		sym->Set(typ, refType, context, (int32) val);
	} else if (refType == TypedValue::REF_INSTANCE) {
		sym->Set(typ, TypedValue::REF_INSTANCE);
		sym->val.stackAddress.offset = (int32)val;
	} else if (refType == TypedValue::REF_POINTER) {
		sym->Set(typ, TypedValue::REF_POINTER);
		sym->val.pointer = val;
	} else if (refType == TypedValue::REF_VALUE) {
		sym->Set(typ, TypedValue::REF_VALUE);
		if (typ == TypedValue::S_FLOAT) {
			if (val)
				sym->SetValue(*(float*)val);
			else
				sym->SetValue((float)0);
#ifdef QUA_V_VST_HOST
		} else if (typ == TypedValue::S_VST_PARAM) {
			sym->val.vstParam = (int)val;
#endif
		} else {
			sym->SetValue(val);
		}
	}
	sym->SetDefaultBounds();
	sym->isClone = isClone;
	sym->isHidden = false;
	sym->isEnveloped = isEnveloped;
//	sym->controlInfo = nullptr;
	sym->indirection = ndim;
	if (sym->size) {
		sym->size[0] = GetTypeSize(typ);
	}
	
	if (context) {
		sym->sibling = nullptr;
		StabEnt		**qq=&context->children;
		for (StabEnt *p = context->children; p; p=p->sibling){
			qq = &p->sibling;
		}
		*qq = sym;
	}

	if (ctlMode != StabEnt::DISPLAY_NOT) {
		sym->controlMode = ctlMode;
		while (context && context->type == TypedValue::S_PARAMGROUP) {
			context = context->context;
		}
		if (context) {
			Stackable	*X = context->StackableValue();

			if (X) {
				X->addController(sym);
			}
		}
	} else {
		sym->controlMode = StabEnt::DISPLAY_NOT;
	}

    if (debug_symtab)
		fprintf(stderr, "!%x\n", (unsigned)sym);
    return sym;
}


void
define_global_symbols()
{
////////////////////////////////
// define global sybols
////////////////////////////////
    class Time		zero;
    Note		t;
    StreamNote	x(&zero, &t);
    Ctrl		s;
    StreamCtrl	y(&zero, &s);
    Prog		r;
    StreamProg	z(&zero, &r);
    QuaJoy		q;
    StreamJoy	v(&zero, &q);
    SysC		rr;
    StreamSysC	w(&zero, &rr);
//	fprintf(stderr, "offsets %d %d %d\n",
//			((char *)&x.note - (char *) &x),
//			((char *)&x.note.pitch - (char *)&x.note),
//			((char *)&x.note.pitch - (char *)&x));
    typeSymbolStreamItem = DefineSymbol("Streamitem", TypedValue::S_STREAM_ITEM, 0,
    	0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("tickat", TypedValue::S_INT, 0,
    				(void *)((char *)&x.time.ticks - (char *) &x),
    				typeSymbolStreamItem, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
  
    typeSymbolNote = DefineSymbol("Note", TypedValue::S_NOTE, 0,
    	(void *)((char *)&x.note - (char *) &x),
    	typeSymbolStreamItem, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("pitch", TypedValue::S_BYTE, 0,
				(void *)((char *)&x.note.pitch - (char *)&x.note),
				typeSymbolNote, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("command", TypedValue::S_BYTE, 0,
				(void *)((char *)&x.note.cmd - (char *)&x.note),
				typeSymbolNote, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("duration", TypedValue::S_FLOAT, 0,
				(void *)((char *)&x.note.duration - (char *)&x.note),
				typeSymbolNote, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("dynamic", TypedValue::S_BYTE, 0,
				(void *)((char *)&x.note.dynamic - (char *)&x.note),
				typeSymbolNote, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
	
    typeSymbolCtrl = DefineSymbol("Ctrl", TypedValue::S_CTRL, 0,
    	(void *)((char *)&y.ctrl - (char *) &y),
    	typeSymbolStreamItem, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("no", TypedValue::S_BYTE, 0,
				(void *)((char *)&y.ctrl.controller - (char *)&y.ctrl),
				typeSymbolCtrl, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("value", TypedValue::S_BYTE, 0,
				(void *)((char *)&y.ctrl.amount - (char *)&y.ctrl),
				typeSymbolCtrl, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);

    typeSymbolProg = DefineSymbol("Prog", TypedValue::S_PROG, 0,
    	(void *)((char *)&z.prog - (char *) &z),
    	typeSymbolStreamItem, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("no", TypedValue::S_BYTE, 0,
				(void *)((char *)&z.prog.program - (char *)&z.prog),
				typeSymbolProg, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("bank", TypedValue::S_BYTE, 0,
				(void *)((char *)&z.prog.bank - (char *)&z.prog),
				typeSymbolProg, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("subbank", TypedValue::S_BYTE, 0,
				(void *)((char *)&z.prog.subbank - (char *)&z.prog),
				typeSymbolProg, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
		
    typeSymbolJoy = DefineSymbol("Joy", TypedValue::S_JOY, 0,
    	(void *)((char *)&v.joy - (char *) &v),
    	typeSymbolStreamItem, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("type", TypedValue::S_BYTE, 1,
				(void *)((char *)&v.joy.type - (char *)&v.joy),
				typeSymbolJoy, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("which", TypedValue::S_BYTE, 1,
				(void *)((char *)&v.joy.which - (char *)&v.joy),
				typeSymbolJoy, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("stick", TypedValue::S_BYTE, 1,
				(void *)((char *)&v.joy.stick - (char *)&v.joy),
				typeSymbolJoy, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("axis", TypedValue::S_FLOAT, 1,
				(void *)((char *)&v.joy.value.axis - (char *)&v.joy),
				typeSymbolJoy, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("hat", TypedValue::S_BYTE, 1,
				(void *)((char *)&v.joy.value.hat - (char *)&v.joy),
				typeSymbolJoy, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("button", TypedValue::S_BOOL, 1,
				(void *)((char *)&v.joy.value.button - (char *)&v.joy),
				typeSymbolJoy, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);

	typeSymbolSysCommon = DefineSymbol("sysc", TypedValue::S_SYSC, 0,
    	(void *)((char *)&w.sysC - (char *) &w),
    	typeSymbolStreamItem, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("command", TypedValue::S_BYTE, 0,
				(void *)((char *)&w.sysC.cmd - (char *)&w.sysC),
				typeSymbolSysCommon, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			StabEnt	*ds = DefineSymbol("data", TypedValue::S_BYTE, 1,
				(void *)((char *)&w.sysC.data1 - (char *)&w.sysC),
				typeSymbolSysCommon, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			ds->size[1] = 2;
		
	Clip		*c = new Clip("clip", nullptr);
	typeSymbolClip = c->sym;  
			DefineSymbol("start", TypedValue::S_TIME, 0,
				(void *)((char *)&c->start - (char *)c),
				typeSymbolClip, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);
			DefineSymbol("duration", TypedValue::S_TIME, 0,
				(void *)((char *)&c->duration - (char *)c),
				typeSymbolClip, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_NOT);

	typeSymbolBend = nullptr;
	typeSymbolMsg = nullptr;

	StabEnt	*builtinSim, *builtinParam;

	builtinSim = DefineSymbol("play", TypedValue::S_BUILTIN,
						0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
	builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_GENERIC_PLAYER, 0);

	builtinSim = DefineSymbol("streamplay", TypedValue::S_BUILTIN,
						0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
	builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_STREAM_PLAYER, 0);
	    builtinParam = DefineSymbol("clip", TypedValue::S_CLIP, 1,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_CTL);
	    builtinParam = DefineSymbol("media", TypedValue::S_TAKE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim, TypedValue::REF_STACK, false, false, StabEnt::DISPLAY_CTL);
	    builtinParam = DefineSymbol("position", TypedValue::S_STRANGE_POINTER, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
						builtinSim,	TypedValue::REF_STACK, false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("looptime", TypedValue::S_TIME, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_TIME, 1),
						builtinSim,	TypedValue::REF_STACK, false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("loopstarttime", TypedValue::S_TIME, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_TIME, 1),
						builtinSim,	TypedValue::REF_STACK, false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("state", TypedValue::S_INT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_INT, 1),
						builtinSim,	TypedValue::REF_STACK, false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("rate", TypedValue::S_FLOAT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_FLOAT, 1),
						builtinSim,	TypedValue::REF_STACK, false, false,StabEnt::DISPLAY_CTL);
	    builtinParam = DefineSymbol("loop", TypedValue::S_BYTE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_BYTE, 1),
						builtinSim,	TypedValue::REF_STACK, false, false,StabEnt::DISPLAY_CTL);

	builtinSim = DefineSymbol("markov", TypedValue::S_BUILTIN,
						0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
	builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_MARKOV_PLAYER, 0);
	    builtinParam = DefineSymbol("clip", TypedValue::S_CLIP, 1,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, StabEnt::DISPLAY_CTL);
	    builtinParam = DefineSymbol("media", TypedValue::S_TAKE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, StabEnt::DISPLAY_CTL);
	    builtinParam = DefineSymbol("position", TypedValue::S_STRANGE_POINTER, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("looptime", TypedValue::S_TIME, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_TIME, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("loopstarttime", TypedValue::S_TIME, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_TIME, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("state", TypedValue::S_INT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_INT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("rate", TypedValue::S_FLOAT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_FLOAT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false, StabEnt::DISPLAY_CTL);
	    builtinParam = DefineSymbol("loop", TypedValue::S_BYTE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_BYTE, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false, StabEnt::DISPLAY_CTL);

	builtinSim = DefineSymbol("sampleplay", TypedValue::S_BUILTIN,
						0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
	builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_SAMPLE_PLAYER, 0);
	    builtinParam = DefineSymbol("clip", TypedValue::S_CLIP, 1,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, StabEnt::DISPLAY_CTL);
	    builtinParam = DefineSymbol("media", TypedValue::S_TAKE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("position", TypedValue::S_INT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_INT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("state", TypedValue::S_INT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_INT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("gain", TypedValue::S_FLOAT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_FLOAT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(1), TypedValue::Float(1));
	    builtinParam = DefineSymbol("pan", TypedValue::S_FLOAT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_FLOAT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Float(-1), TypedValue::Float(0), TypedValue::Float(1));
	    builtinParam = DefineSymbol("loop", TypedValue::S_BYTE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_BYTE, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Int(0), TypedValue::Int(0), TypedValue::Int(64000));
	    builtinParam = DefineSymbol("direction", TypedValue::S_BYTE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_BYTE, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Int(-1), TypedValue::Int(1), TypedValue::Int(1));

	builtinSim = DefineSymbol("play2n", TypedValue::S_BUILTIN,
						0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
	builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_SAMPLE_PLAYER, 0);
	    builtinParam = DefineSymbol("clip", TypedValue::S_CLIP, 1,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, StabEnt::DISPLAY_CTL);
	    builtinParam = DefineSymbol("media", TypedValue::S_TAKE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("position", TypedValue::S_INT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_INT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false, StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("state", TypedValue::S_INT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_INT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false, StabEnt::DISPLAY_NOT);
	    builtinParam = DefineSymbol("gain", TypedValue::S_FLOAT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_FLOAT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false, StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(1), TypedValue::Float(1));
		builtinParam = DefineSymbol("pitch", TypedValue::S_FLOAT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_FLOAT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	(int16)StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Float(-1), TypedValue::Float(0), TypedValue::Float(1));
	    builtinParam = DefineSymbol("pan", TypedValue::S_FLOAT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_FLOAT, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	(int16)StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Float(-1), TypedValue::Float(0), TypedValue::Float(1));
	    builtinParam = DefineSymbol("loop", TypedValue::S_BYTE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_BYTE, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	(int16)StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Int(0), TypedValue::Int(0), TypedValue::Int(64000));
	    builtinParam = DefineSymbol("direction", TypedValue::S_BYTE, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_BYTE, 1),
						builtinSim,	(int16)(TypedValue::REF_STACK),	false, false,	(int16)StabEnt::DISPLAY_CTL);
/////////////////////////////////////////////////////////////////
// lfo
///////////////////////////////////////////////////////////////
	builtinSim = DefineSymbol("lfo", TypedValue::S_BUILTIN,
						0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
	builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_BUILTIN, Block::BUILTIN_LFO);
	    builtinParam = DefineSymbol("rate", TypedValue::S_FLOAT, 0,	// rate in floating point beats
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Float(.125), TypedValue::Float(1), TypedValue::Float(128));
	    builtinParam = DefineSymbol("depth", TypedValue::S_FLOAT, 0,
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(0.5), TypedValue::Float(1));
	    builtinParam = DefineSymbol("phase", TypedValue::S_FLOAT, 0,	// phase, 0..1
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
		builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(0), TypedValue::Float(1));
	    builtinParam = DefineSymbol("trigger", TypedValue::S_BYTE, 0,	// phase, 0..1
						(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    				builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
///////////////////////////////////////////////////////////////
		builtinSim = DefineSymbol("sinlfo", TypedValue::S_BUILTIN,
							0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
		builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_BUILTIN, Block::BUILTIN_SINLFO);
			builtinParam = DefineSymbol("rate", TypedValue::S_FLOAT, 0,	// rate in floating point beats
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(.125), TypedValue::Float(1), TypedValue::Float(128));
			builtinParam = DefineSymbol("depth", TypedValue::S_FLOAT, 0,
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(0.5), TypedValue::Float(1));
///////////////////////////////////////////////////////////////
		builtinSim = DefineSymbol("sawlfo", TypedValue::S_BUILTIN,
							0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
		builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_BUILTIN, Block::BUILTIN_SAWLFO);
			builtinParam = DefineSymbol("rate", TypedValue::S_FLOAT, 0,	// rate in floating point beats
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(.125), TypedValue::Float(1), TypedValue::Float(128));
			builtinParam = DefineSymbol("depth", TypedValue::S_FLOAT, 0,
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(0.5), TypedValue::Float(1));
///////////////////////////////////////////////////////////////
		builtinSim = DefineSymbol("explfo", TypedValue::S_BUILTIN,
							0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
		builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_BUILTIN, Block::BUILTIN_EXPLFO);
			builtinParam = DefineSymbol("rate", TypedValue::S_FLOAT, 0,	// rate in floating point beats
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(.125), TypedValue::Float(1), TypedValue::Float(128));
			builtinParam = DefineSymbol("depth", TypedValue::S_FLOAT, 0,
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(0.5), TypedValue::Float(1));
///////////////////////////////////////////////////////////////
		builtinSim = DefineSymbol("squarelfo", TypedValue::S_BUILTIN,
							0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
		builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_BUILTIN, Block::BUILTIN_SQUARELFO);
			builtinParam = DefineSymbol("rate", TypedValue::S_FLOAT, 0,	// rate in floating point beats
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(.125), TypedValue::Float(1), TypedValue::Float(128));
			builtinParam = DefineSymbol("depth", TypedValue::S_FLOAT, 0,
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(0.5), TypedValue::Float(1));
///////////////////////////////////////////////////////////////
		builtinSim = DefineSymbol("randlfo", TypedValue::S_BUILTIN,
							0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
		builtinSim->BuiltinValue()->Set(new Stackable(builtinSim), Block::C_BUILTIN, Block::BUILTIN_RANDLFO);
			builtinParam = DefineSymbol("rate", TypedValue::S_FLOAT, 0,	// rate in floating point beats
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(.125), TypedValue::Float(1), TypedValue::Float(128));
			builtinParam = DefineSymbol("depth", TypedValue::S_FLOAT, 0,
							(void*)AllocStack(builtinSim,(base_type_t)TypedValue::S_STRANGE_POINTER, 1),
	    					builtinSim,(int16)(TypedValue::REF_STACK),false, false, (int16)StabEnt::DISPLAY_CTL);
			builtinParam->SetBounds(TypedValue::Float(0), TypedValue::Float(0.5), TypedValue::Float(1));
//////////////////////////////////////////////////////////////////
// rest of builtins. mainly maths functions of 1 variable
// stuff that probly won't appear as interface controls directly
/////////////////////////////////////////////////////////////////
			for (auto ci = builtinCommandIndex.begin(); ci != builtinCommandIndex.end(); ++ci) {
			builtinSim = DefineSymbol(ci->first, TypedValue::S_BUILTIN,
							0, 0, nullptr, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT);
			builtinSim->BuiltinValue()->Set(nullptr, Block::C_BUILTIN, ci->second);
	}

//	    for (i=1; i<=ndimensions; i++) {
//	    	builtinParam->size[i] = dimensions[i-1];
//	    }

	glob.PushContext(typeSymbolStreamItem);
	glob.PushContext(typeSymbolNote);
//	glob.PushContext(typeSymbolJoy);
	
//	typeSymbolStreamItem->Dump(stderr, 0);
}
