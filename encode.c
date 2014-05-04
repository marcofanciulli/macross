/* WARNING: this file has not yet been separated into 6502 and non-6052 parts */
/*
	encode.c -- Routines to encode expressions for Macross object files.

	Chip Morningstar -- Lucasfilm Ltd.

	8-November-1985
*/

#include "macrossTypes.h"
#include "macrossGlobals.h"
#include "y.tab.h"
#include "slinkyExpressions.h"

#define nullEncode(thing)   if (thing==NULL) return(TRUE);
#define byteOp(op) ((op)-256)

bool	encodingFunction;

  bool
encodeByte(aByte)
  byte	aByte;
{
	if (expressionBufferSize < EXPRESSION_BUFFER_LIMIT) {
		expressionBuffer[expressionBufferSize++] = aByte;
		return(TRUE);
	} else {
		error(EXPRESSION_TOO_BIG_TO_FIT_IN_OBJECT_ERROR);
		return(FALSE);
	}
}

  bool
encodeBigword(bigword)
  int	bigword;
{
	int	i;
	for (i=0; i<sizeof(int); ++i) {
		if (!encodeByte(bigword & 0xFF))
			return(FALSE);
		bigword >>= 8;
	}
	return(TRUE);
}

  bool
encodeAssignmentTerm(assignmentTerm, kindOfFixup)
  binopTermType	*assignmentTerm;
  fixupKindType	 kindOfFixup;
{
	nullEncode(assignmentTerm);
	if ((assignmentKindType)assignmentTerm->binop != ASSIGN_ASSIGN) {
		error(FUNNY_ASSIGNMENT_KIND_IN_OBJECT_EXPRESSION_ERROR);
		return(FALSE);
	}
	return(
		encodeByte(BINOP_TAG) &&
		encodeByte(byteOp(ASSIGN)) &&
		encodeIdentifier(assignmentTerm->leftArgument) &&
		encodeExpression(assignmentTerm->rightArgument)
	);
}

  bool
encodeBinopTerm(binopTerm, isTopLevel, kindOfFixup)
  binopTermType	*binopTerm;
  bool		 isTopLevel;
  fixupKindType	 kindOfFixup;
{
	bool	encodeExpression();

	nullEncode(binopTerm);
	return (
		encodeByte(BINOP_TAG) &&
		encodeByte(byteOp(binopTerm->binop)) &&
		encodeExpression(binopTerm->leftArgument) &&
		encodeExpression(binopTerm->rightArgument)
	);
}

  bool
encodeCondition(condition)
  conditionType	condition;
{
	return(
		encodeByte(CONDITION_CODE_TAG) &&
		encodeByte(condition)
	);
}

  int
functionNumber(function)
  functionDefinitionType	*function;
{
	if (function->ordinal == -1) {
		function->ordinal = externalFunctionCount++;
		if (externalFunctionList == NULL) {
			externalFunctionList = endOfExternalFunctionList =
				function;
		} else {
			endOfExternalFunctionList->nextExternalFunction =
				function;
			endOfExternalFunctionList = function;
		}
	}
	return(function->ordinal);
}

  bool
encodeFunctionCall(functionCall)
  functionCallTermType	*functionCall;
{
	functionDefinitionType	*theFunction;
	int			 functionOrdinal;
	symbolInContextType	*workingContext;
	operandListType		*parameterList;

	symbolInContextType	*getWorkingContext();

	nullEncode(functionCall);
	workingContext = getWorkingContext(functionCall->functionName);
	if (isFunction(workingContext)) {
		if (!encodeByte(FUNCTION_CALL_TAG))
			return(FALSE);
		theFunction = (functionDefinitionType *)workingContext->
			value->value;
		if (!encodeBigword(functionNumber(theFunction)))
			return(FALSE);
	} else if (isBuiltInFunction(workingContext)) {
		functionOrdinal = workingContext->value->value;
		if (builtInFunctionTable[functionOrdinal].isSpecialFunction)
			return(encodeValue((*builtInFunctionTable[
				functionOrdinal].functionEntry)(functionCall->
				parameters, NO_FIXUP)));
		if (!encodeByte(BUILTIN_FUNCTION_CALL_TAG))
			return(FALSE);
		if (builtInFunctionTable[functionOrdinal].ordinal < 0) {
			error(BUILT_IN_FUNCTION_NOT_AVAILABLE_IN_OBJECT_ERROR,
				builtInFunctionTable[functionOrdinal].
				functionName);
			return(FALSE);
		} else if (!encodeBigword(builtInFunctionTable[
				functionOrdinal].ordinal)) {
			return(FALSE);
		}
	} else {
		error(NOT_A_FUNCTION_ERROR, symbName(functionCall->
			functionName));
		return(FALSE);
	}
	parameterList = functionCall->parameters;
	if (!encodeByte(countParameters(parameterList)))
		return(FALSE);
	while (parameterList != NULL)
		if (!encodeOperand(parameterList))
			return(FALSE);
		else
			parameterList = parameterList->nextOperand;
	return(TRUE);
}

  bool
encodeHere()
{
	return(encodeByte(HERE_TAG));
}

  bool
encodeIdentifier(identifier)
  symbolTableEntryType	*identifier;
{
	symbolInContextType	*workingContext;
	environmentType		*saveEnvironment;
	bool			 result;

	nullEncode(identifier);

	if (symbName(identifier)[0] == '$') {
		error(TEMP_SYMBOL_IN_OBJECT_ERROR, symbName(identifier));
		return(FALSE);
	}
	if (encodingFunction) {
		return(encodeByte(IDENTIFIER_TAG) &&
		       encodeBigword(identifier->ordinal));
	}
	if ((workingContext = getWorkingContext(identifier)) == NULL) {
		error(UNDEFINED_SYMBOL_ERROR, symbName(identifier));
		return(FALSE);
	}
	if (workingContext->usage == FUNCTION_SYMBOL || workingContext->usage
			== BUILT_IN_FUNCTION_SYMBOL) {
		error(FUNCTION_IS_NOT_A_VALUE_ERROR, symbName(identifier));
		return(FALSE);
	}
	if (workingContext->value == NULL) {
		error(UNDEFINED_SYMBOL_ERROR, symbName(identifier));
		return(FALSE);
	}
	if (workingContext->value->kindOfValue == UNDEFINED_VALUE) {
		if (workingContext->attributes & GLOBAL_ATT) {
			return(encodeByte(IDENTIFIER_TAG) &&
			       encodeBigword(identifier->ordinal));
		} else {
			error(UNDEFINED_SYMBOL_ERROR, symbName(identifier));
			return(FALSE);
		}
	}
	if (workingContext->value->kindOfValue == RELOCATABLE_VALUE) {
		return(encodeByte(IDENTIFIER_TAG) &&
		       encodeBigword(identifier->ordinal));
	}
	if (workingContext->value->kindOfValue == FAIL) {
		error(UNASSIGNED_SYMBOL_ERROR, symbName(identifier));
		return(FALSE);
	}
	if (workingContext->value->kindOfValue == OPERAND_VALUE) {
		saveEnvironment = currentEnvironment;
		if (workingContext->usage == ARGUMENT_SYMBOL) {
			currentEnvironment = currentEnvironment->
				previousEnvironment;
		}
		result = encodeOperand(workingContext->value->value);
		currentEnvironment = saveEnvironment;
		return(result);
	}
	if (workingContext->value->kindOfValue == BLOCK_VALUE) {
		error(BLOCK_OPERAND_IN_OBJECT_EXPRESSION_ERROR);
		return(FALSE);
	}
	return(encodeValue(workingContext->value));
}

  bool
encodeNumber(number)
  numberTermType	number;
{
	return(
		encodeByte(NUMBER_TAG) &&
		encodeBigword(number)
	);
}

  bool
encodeRelocatableNumber(number)
  numberTermType	number;
{
	return(
		encodeByte(RELOCATABLE_TAG) &&
		encodeBigword(number)
	);
}

  bool
encodeOperand(operand)
  operandType	*operand;
{
	switch (operand->kindOfOperand) {
		case EXPRESSION_OPND:
		case IMMEDIATE_OPND:
		case INDIRECT_OPND:
		case POST_INDEXED_Y_OPND:
		case PRE_INDEXED_X_OPND:
		case X_INDEXED_OPND:
		case Y_INDEXED_OPND:
			return(encodeExpression(operand->theOperand));

		case A_REGISTER_OPND:
		case X_REGISTER_OPND:
		case Y_REGISTER_OPND:
			error(REGISTER_OPERAND_IN_OBJECT_EXPRESSION_ERROR);
			return(FALSE);

		case X_SELECTED_OPND:
		case Y_SELECTED_OPND:
		case PRE_SELECTED_X_OPND:
			error(SELECTION_OPERAND_IN_OBJECT_EXPRESSION_ERROR);
			return(FALSE);

		case STRING_OPND:
			return(encodeString(operand->theOperand));

		case BLOCK_OPND:
			error(BLOCK_OPERAND_IN_OBJECT_EXPRESSION_ERROR);
			return(FALSE);
	}
}

  bool
encodePostopTerm(postopTerm)
  postOpTermType	*postopTerm;
{
	nullEncode(postopTerm);
	return(
		encodeByte(POSTOP_TAG) &&
		encodeByte(byteOp(postopTerm->postOp)) &&
		encodeExpression(postopTerm->postOpArgument)
	);
}

  bool
encodePreopTerm(preopTerm)
  preOpTermType	*preopTerm;
{
	nullEncode(preopTerm);
	return(
		encodeByte(PREOP_TAG) &&
		encodeByte(byteOp(preopTerm->preOp)) &&
		encodeExpression(preopTerm->preOpArgument)
	);
}

  bool
encodeString(string)
  stringType	*string;
{
	if (!encodeByte(STRING_TAG))
		return(FALSE);
	while (*string != '\0') {
		if (!encodeByte(*string++))
			return(FALSE);
	}
	return(encodeByte('\0'));
}

  bool
encodeUnopTerm(unopTerm)
  unopTermType	*unopTerm;
{
	nullEncode(unopTerm);
	return(
		encodeByte(UNOP_TAG) &&
		encodeByte(byteOp(unopTerm->unop)) &&
		encodeExpression(unopTerm->unopArgument)
	);
}

  bool
encodeValue(value)
  valueType	*value;
{
	switch (value->kindOfValue) {
		case ABSOLUTE_VALUE:
			return(encodeNumber(value->value));

		case RELOCATABLE_VALUE:
			return(encodeRelocatableNumber(value->value));

		case OPERAND_VALUE:
			return(encodeOperand(value->value));

		case STRING_VALUE:
			return(encodeString(value->value));

		case CONDITION_VALUE:
			return(encodeCondition(value->value));

		case DATA_VALUE:
		case BSS_VALUE:
		case STRUCT_VALUE:
		case FIELD_VALUE:
		case MACRO_VALUE:
		case UNDEFINED_VALUE:
		case FUNCTION_VALUE:
		case BLOCK_VALUE:
		case BUILT_IN_FUNCTION_VALUE:
		case ARRAY_VALUE:
		case FAIL:
			error(WRONG_KIND_OF_VALUE_IN_OBJECT_EXPRESSION_ERROR,
				valueKindString(value->kindOfValue));
			return(FALSE);
	}
}

  bool
encodeExpression(expression)
  expressionType	*expression;
{
	nullEncode(expression);
	switch (expression->kindOfTerm) {

	case ARRAY_EXPR:
		error(ARRAY_TERM_IN_OBJECT_EXPRESSION_ERROR);
		return(FALSE);
		break;

	case ASSIGN_EXPR:
		return(encodeAssignmentTerm(expression->expressionTerm));
		break;

	case BINOP_EXPR:
		return(encodeBinopTerm(expression->expressionTerm));
		break;

	case CONDITION_CODE_EXPR:
		return(encodeCondition(expression->expressionTerm));
		break;

	case FUNCTION_CALL_EXPR:
		return(encodeFunctionCall(expression->expressionTerm));
		break;

	case HERE_EXPR:
		return(encodeHere());
		break;

	case IDENTIFIER_EXPR:
		return(encodeIdentifier(expression->expressionTerm));
		break;

	case NUMBER_EXPR:
		return(encodeNumber(expression->expressionTerm));
		break;

	case POSTOP_EXPR:
		return(encodePostopTerm(expression->expressionTerm));
		break;

	case PREOP_EXPR:
		return(encodePreopTerm(expression->expressionTerm));
		break;

	case SUBEXPRESSION_EXPR:
		encodeExpression(expression->expressionTerm);
		break;

	case STRING_EXPR:
		return(encodeString(expression->expressionTerm));
		break;

	case UNOP_EXPR:
		return(encodeUnopTerm(expression->expressionTerm));
		break;

	case VALUE_EXPR:
		return(encodeValue(expression->expressionTerm));
		break;

	default:
		botch("encodeExpression: funny expression kind %d\n",
			expression->kindOfTerm);
		break;
	}
}

  bool
encodeAssertStatement(assertStatement)
  assertStatementBodyType	*assertStatement;
{
	return(
		encodeByte(ASSERT_TAG) &&
		encodeExpression(assertStatement->condition) &&
		encodeExpression(assertStatement->message)
	);
}

  bool
encodeFreturnStatement(freturnStatement)
  freturnStatementBodyType	*freturnStatement;
{
	return(
		encodeByte(FRETURN_TAG) &&
		encodeExpression(freturnStatement)
	);
}

  bool
encodeMdefineStatement(mdefineStatement)
  defineStatementBodyType	*mdefineStatement;
{
	return(
		encodeByte(MDEFINE_TAG) &&
		encodeIdentifier(mdefineStatement->theSymbol) &&
		encodeExpression(mdefineStatement->theValue)
	);
}

  bool
encodeMdoUntilStatement(mdoUntilStatement)
  mdoUntilStatementBodyType	*mdoUntilStatement;
{
	return(
		encodeByte(MDOUNTIL_TAG) &&
		encodeExpression(mdoUntilStatement->mdoUntilCondition) &&
		encodeBlock(mdoUntilStatement->mdoUntilLoop)
	);
}

  bool
encodeMdoWhileStatement(mdoWhileStatement)
  mdoWhileStatementBodyType	*mdoWhileStatement;
{
	return(
		encodeByte(MDOWHILE_TAG) &&
		encodeExpression(mdoWhileStatement->mdoWhileCondition) &&
		encodeBlock(mdoWhileStatement->mdoWhileLoop)
	);
}

  bool
encodeMforStatement(mforStatement)
  mforStatementBodyType	*mforStatement;
{
	return(
		encodeByte(MFOR_TAG) &&
		encodeExpression(mforStatement->initExpression) &&
		encodeExpression(mforStatement->testExpression) &&
		encodeExpression(mforStatement->incrExpression) &&
		encodeBlock(mforStatement->forLoop)
	);
}

  bool
encodeMifStatement(mifStatement)
  mifStatementBodyType	*mifStatement;
{
	return(
		encodeByte(MIF_TAG) &&
		encodeExpression(mifStatement->mifCondition) &&
		encodeBlock(mifStatement->mifConsequence) &&
		encodeBlock(mifStatement->mifContinuation)
	);
}

  bool
encodeMswitchStatement(mswitchStatement)
  mswitchStatementBodyType	*mswitchStatement;
{
    caseListType	*caseList;
    caseType		*theCase;
    expressionListType	*tagExpressionList;

    if (!(encodeByte(MSWITCH_TAG) && encodeExpression(mswitchStatement->
	    switchExpression)))
        return(FALSE);
    for (caseList=mswitchStatement->cases; caseList!=NULL; caseList=caseList->
	    nextCase) {
        theCase = caseList->theCase;
        for (tagExpressionList=theCase->caseTags; tagExpressionList!=NULL;
		tagExpressionList=tagExpressionList->nextExpression) {
            if (!encodeExpression(tagExpressionList->theExpression))
                return(FALSE);
        }
        if (!encodeBlock(theCase->caseBody))
            return(FALSE);
    }
    return(encodeByte(END_TAG));
}

  bool
encodeMvariableStatement(mvariableStatement)
  mvariableStatementBodyType	*mvariableStatement;
{
	int	length;	

	if ((length=expressionListLength(mvariableStatement->theValue) > 1) ||
			mvariableStatement->theDimension!=NULL) {
		error(ARRAY_MVARIABLE_IN_OBJECT_FUNCTION_ERROR);
		return(FALSE);
	}
	if (!(encodeByte(MVARIABLE_TAG) && encodeIdentifier(mvariableStatement->
			theSymbol)))
		return(FALSE);
	if (length == 1)
		return(encodeExpression(mvariableStatement->theValue->
			theExpression));
	else
		return(encodeExpression(NULL));
}

  bool
encodeMwhileStatement(mwhileStatement)
  mwhileStatementBodyType	*mwhileStatement;
{
	return(
		encodeByte(MWHILE_TAG) &&
		encodeExpression(mwhileStatement->mwhileCondition) &&
		encodeBlock(mwhileStatement->mwhileLoop)
	);
}

  bool
encodeStatement(statement)
  statementType	*statement;
{
	switch(statement->kindOfStatement) {

	case ALIGN_STATEMENT:
	case BLOCK_STATEMENT:
	case BYTE_STATEMENT:
	case CONSTRAIN_STATEMENT:
	case DBYTE_STATEMENT:
	case DEFINE_STATEMENT:
	case DO_UNTIL_STATEMENT:
	case DO_WHILE_STATEMENT:
	case EXTERN_STATEMENT:
	case FUNCTION_STATEMENT:
	case IF_STATEMENT:
	case INCLUDE_STATEMENT:
	case INSTRUCTION_STATEMENT:
	case LONG_STATEMENT:
	case MACRO_STATEMENT:
	case ORG_STATEMENT:
	case REL_STATEMENT:
	case START_STATEMENT:
	case STRING_STATEMENT:
	case STRUCT_STATEMENT:
	case TARGET_STATEMENT:
	case UNDEFINE_STATEMENT:
	case VARIABLE_STATEMENT:
	case WHILE_STATEMENT:
	case WORD_STATEMENT:
		error(ILLEGAL_STATEMENT_IN_OBJECT_FILE_FUNCTION_ERROR,
			statementKindString(statement->kindOfStatement));
		return(FALSE);

	case ASSERT_STATEMENT:
		return(encodeAssertStatement(statement->statementBody));

	case FRETURN_STATEMENT:
		return(encodeFreturnStatement(statement->statementBody));

	case GROUP_STATEMENT:
		return(encodeBlock(statement->statementBody));

	case MDEFINE_STATEMENT:
		return(encodeMdefineStatement(statement->statementBody));

	case MDO_UNTIL_STATEMENT:
		return(encodeMdoUntilStatement(statement->statementBody));

	case MDO_WHILE_STATEMENT:
		return(encodeMdoWhileStatement(statement->statementBody));

	case MFOR_STATEMENT:
		return(encodeMforStatement(statement->statementBody));

	case MIF_STATEMENT:
		return(encodeMifStatement(statement->statementBody));

	case MSWITCH_STATEMENT:
		return(encodeMswitchStatement(statement->statementBody));

	case MVARIABLE_STATEMENT:
		return(encodeMvariableStatement(statement->statementBody));

	case MWHILE_STATEMENT:
		return(encodeMwhileStatement(statement->statementBody));

	case NULL_STATEMENT:
		return(TRUE);

	case PERFORM_STATEMENT:
		return(encodeExpression(statement->statementBody));

	default:
		botch("encodeStatementBody doesn't know kind %d\n",
			statement->kindOfStatement);
		return(FALSE);
	}
}

  bool
encodeBlock(block)
  blockType	*block;
{
	if (!encodeByte(BLOCK_TAG))
		return(FALSE);
	while (block != NULL) {
		if (!encodeStatement(block))
			return(FALSE);
		block = block->nextStatement;
	}
	return(encodeByte(END_TAG));
}