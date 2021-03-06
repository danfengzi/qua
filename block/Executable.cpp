#include "qua_version.h"

#include "StdDefs.h"
#include "Sym.h"
#include "Executable.h"
#include "QuaDisplay.h"

#include "Qua.h"
#include "Block.h"
#include "Instance.h"


Event::Event(char *nm, StabEnt *ctxt):
	Stackable(
		DefineSymbol(nm, TypedValue::S_EVENT, 0,
				this, ctxt, TypedValue::REF_VALUE, false, false, StabEnt::DISPLAY_NOT)
		)
{
	block = nullptr;
}

Event::~Event()
{
//	if (block)
//		block->DeleteAll();
	block = nullptr;
}

void
Event::operator=(Event&s)
{
	stackSize = s.stackSize;
	if (s.block) {
		block = new Block(s.block, sym, true);
		if (!block->Init(nullptr)) {
			internalError("Can't initialise %s block", sym->name);
		}
	}
}

void
Event::operator=(Block *b)
{
	block = b;
}

Executable::Executable(StabEnt *S):
	Stackable(S)
{
	mainBlock = nullptr;
}

Executable::~Executable()
{
	if (mainBlock) mainBlock->DeleteAll();
}


