/*
 *  Copyright 2007-2014 The OpenMx Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


/***********************************************************
* 
*  omxAlgebra.cc
*
*  Created: Timothy R. Brick 	Date: 2008-11-13 12:33:06
*
*	Algebras are a subclass of data matrix that evaluates
*   itself anew at each iteration, so that any changes to
*   free parameters can be incorporated into the update.
*
**********************************************************/

#include "omxMatrix.h"
#include "omxFitFunction.h"

static void
omxAlgebraAllocArgs(omxAlgebra *oa, int numArgs)
{
	if (numArgs <= 0) {
		oa->numArgs = 0;
		oa->algArgs = NULL;
		return;
	}

	if(oa->algArgs != NULL) {
		if (oa->numArgs < numArgs)
			error("omxAlgebra: %d args requested but %d available",
			      numArgs, oa->numArgs);
		return;
	}

	oa->numArgs = numArgs;
	oa->algArgs = (omxMatrix**) R_alloc(numArgs, sizeof(omxMatrix*));
	memset(oa->algArgs, 0, sizeof(omxMatrix*) * numArgs);  //remove debug TODO
}

omxMatrix* omxInitAlgebra(omxAlgebra *oa, omxState* os) {

	omxMatrix* om = omxInitMatrix(NULL, 0, 0, TRUE, os);
	
	omxInitAlgebraWithMatrix(oa, om);

	return om;
}

void omxInitAlgebraWithMatrix(omxAlgebra *oa, omxMatrix *om) {
	
	if(oa == NULL) {
		oa = (omxAlgebra*) R_alloc(1, sizeof(omxAlgebra));
	}
	
	omxAlgebraAllocArgs(oa, 0);
	oa->funWrapper = NULL;
	oa->matrix = om;
	om->algebra = oa;
}

void omxDuplicateAlgebra(omxMatrix* tgt, omxMatrix* src, omxState* newState) {

    if(src->algebra != NULL) {
		omxFillMatrixFromMxAlgebra(tgt, src->algebra->sexpAlgebra, src->name);
    } else if(src->fitFunction != NULL) {
        omxDuplicateFitMatrix(tgt, src, newState);
    }

}

void omxFreeAlgebraArgs(omxAlgebra *oa) {
	/* Completely destroy the algebra tree */
	
	int j;
	for(j = 0; j < oa->numArgs; j++) {
		omxFreeAllMatrixData(oa->algArgs[j]);
		oa->algArgs[j] = NULL;
	}
	omxAlgebraAllocArgs(oa, 0);
	oa->matrix = NULL;
}

static void dispatchOp(omxAlgebra *oa)
{
	if(oa->funWrapper == NULL) {
		if(oa->numArgs != 1) error("Internal Error: Empty algebra evaluated");
		if(OMX_DEBUG_ALGEBRA) { omxPrint(oa->algArgs[0], "Copy no-op algebra"); }
		omxCopyMatrix(oa->matrix, oa->algArgs[0]);
	} else {
		if(OMX_DEBUG_ALGEBRA) { 
			std::string buf;
			for (int ax=0; ax < oa->numArgs; ++ax) {
				if (ax) buf += ", ";
				const char *argname = oa->algArgs[ax]->name;
				buf += argname? argname : "?";
			}
			mxLog("Algebra '%s' %s(%s)", oa->matrix->name, oa->oate->rName, buf.c_str());
		}
		(*((void(*)(omxMatrix**, int, omxMatrix*))oa->funWrapper))(oa->algArgs, (oa->numArgs), oa->matrix);
	}

	omxMarkClean(oa->matrix);
	
	if(OMX_DEBUG_ALGEBRA) {
		std::string name = string_snprintf("Algebra '%s' result", oa->matrix->name);
		omxAlgebraPrint(oa, name.c_str());
	}
}

void omxAlgebraRecompute(omxAlgebra *oa)
{
	for(int j = 0; j < oa->numArgs; j++) omxRecompute(oa->algArgs[j]);
	dispatchOp(oa);
}

void omxAlgebraForceCompute(omxAlgebra *oa)
{
	for(int j = 0; j < oa->numArgs; j++) omxForceCompute(oa->algArgs[j]);
	dispatchOp(oa);
}

omxMatrix* omxNewMatrixFromMxAlgebra(SEXP alg, omxState* os, const char *name) {

	omxMatrix *om = omxInitMatrix(NULL, 0, 0, TRUE, os);

	om->hasMatrixNumber = 0;
	om->matrixNumber = 0;	

	omxFillMatrixFromMxAlgebra(om, alg, name);
	
	return om;
}

static void
omxFillAlgebraFromTableEntry(omxAlgebra *oa, const omxAlgebraTableEntry* oate, const int realNumArgs) {
	/* TODO: check for full initialization */
	if(oa == NULL) error("Internal Error: Null Algebra Detected in fillAlgebra.");

	oa->oate = oate;
	oa->funWrapper = oate->funWrapper;
	omxAlgebraAllocArgs(oa, oate->numArgs==-1? realNumArgs : oate->numArgs);
}

void omxFillMatrixFromMxAlgebra(omxMatrix* om, SEXP algebra, const char *name) {

	int value;
	omxAlgebra *oa = NULL;
	SEXP algebraOperator, algebraArg, algebraElt;
	
	PROTECT(algebraOperator = AS_INTEGER(VECTOR_ELT(algebra, 0)));
	value = INTEGER(algebraOperator)[0];

	if(OMX_DEBUG) {mxLog("Creating Algebra from Sexp.");}

	if(value > 0) { 			// This is an operator.
		oa = (omxAlgebra*) R_alloc(1, sizeof(omxAlgebra));
		omxInitAlgebraWithMatrix(oa, om);
		if(OMX_DEBUG) {mxLog("Retrieving Table Entry %d.", value);}
		const omxAlgebraTableEntry* entry = &(omxAlgebraSymbolTable[value]);
		if(OMX_DEBUG) {mxLog("Table Entry %d is %s.", value, entry->opName);}
		omxFillAlgebraFromTableEntry(oa, entry, length(algebra) - 1);
		for(int j = 0; j < oa->numArgs; j++) {
			PROTECT(algebraArg = VECTOR_ELT(algebra, j+1));
				oa->algArgs[j] = omxAlgebraParseHelper(algebraArg, om->currentState, NULL);
		}
	} else {		// This is an algebra pointer, and we're a No-op algebra.
		/* TODO: Optimize this by eliminating no-op algebras entirely. */
		PROTECT(algebraElt = VECTOR_ELT(algebra, 1));
		
		if(!IS_INTEGER(algebraElt)) {   			// A List: only happens if bad optimization has occurred.
			warning("Internal Error: Algebra has been passed incorrectly: detected NoOp: (Operator Arg ...)\n");
			omxFillMatrixFromMxAlgebra(om, algebraElt, NULL);		// Collapse the no-op algebra
		} else {			// Still a No-op.  Sadly, we have to keep it that way.
			
			PROTECT(algebraOperator = AS_INTEGER(algebraElt));
			value = INTEGER(algebraOperator)[0];
			
			oa = (omxAlgebra*) R_alloc(1, sizeof(omxAlgebra));
			omxInitAlgebraWithMatrix(oa, om);
			omxAlgebraAllocArgs(oa, 1);
			
			if(value < 0) {
				value = ~value;					// Bitwise reverse of number--this is a matrix index
				oa->algArgs[0] = (oa->matrix->currentState->matrixList[value]);
			} else {
				oa->algArgs[0] = (oa->matrix->currentState->algebraList[value]);
			}
		}
	}
	om->name        = name;
	oa->sexpAlgebra = algebra;
}

omxMatrix* omxAlgebraParseHelper(SEXP algebraArg, omxState* os, const char *name) {
	omxMatrix* newMat;
	if(OMX_DEBUG) { mxLog("Helper: processing next arg..."); }
	
	if(!IS_INTEGER(algebraArg)) {
		if(OMX_DEBUG) { mxLog("Helper detected list element.  Recursing."); }
		newMat = omxNewMatrixFromMxAlgebra(algebraArg, os, name);
	} else {
		newMat = omxMatrixLookupFromState1(algebraArg, os);
	}
	
	return(newMat);
}

void omxAlgebraPrint(omxAlgebra* oa, const char* d) {
	std::string name = string_snprintf("%s_%d", d, oa->numArgs);
	omxPrintMatrix(oa->matrix, name.c_str());
}

omxMatrix* omxMatrixLookupFromState1(SEXP matrix, omxState* os) {
	int value = 0;
	omxMatrix* output = NULL;

	if (IS_INTEGER(matrix)) {
		SEXP intMatrix;
		PROTECT(intMatrix = AS_INTEGER(matrix));
		value = INTEGER(intMatrix)[0];
		if(value == NA_INTEGER) {
			return NULL;
		}
	} else if (IS_NUMERIC(matrix)) {
		SEXP numericMatrix;
		PROTECT(numericMatrix = AS_NUMERIC(matrix));
		value = (int) REAL(numericMatrix)[0];
		if(value == NA_INTEGER) {
			return NULL;
		}
	} else if (matrix == R_NilValue) {
		return NULL;
	} else if (isString(matrix)) {
		error("Internal error: string passed to omxMatrixLookupFromState1, did you forget to call imxLocateIndex?");
	} else {
		error("Internal error: unknown type passed to omxMatrixLookupFromState1");
	}		
	if (value >= 0) {
		output = os->algebraList[value];
	} else {
		output = os->matrixList[~value];
	}
	
	return output;
}

omxMatrix* omxNewAlgebraFromOperatorAndArgs(int opCode, omxMatrix* args[], int numArgs, omxState* os) {
	
	if(OMX_DEBUG) {mxLog("Generating new algebra from opcode %d (%s).", opCode, omxAlgebraSymbolTable[opCode].rName);}
	omxMatrix *om;
	omxAlgebra *oa = (omxAlgebra*) R_alloc(1, sizeof(omxAlgebra));
	omxAlgebraTableEntry* entry = (omxAlgebraTableEntry*)&(omxAlgebraSymbolTable[opCode]);
	if(entry->numArgs >= 0 && entry->numArgs != numArgs) {
		error("Internal error: incorrect number of arguments passed to algebra %s.", entry->rName);
	}
	
	om = omxInitAlgebra(oa, os);
	omxFillAlgebraFromTableEntry(oa, entry, entry->numArgs);
	om->name = entry->opName;

	if(OMX_DEBUG) {mxLog("Calculating args for %s.", entry->rName);}
	omxAlgebraAllocArgs(oa, numArgs);
	
	if(OMX_DEBUG) {mxLog("Populating args for %s.", entry->rName);}
	
	for(int i = 0; i < numArgs;i++) {
		oa->algArgs[i] = args[i];
	}
	
	omxMarkDirty(om);
	
	return om;
	
}

