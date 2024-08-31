// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2000-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*
* File wrtjava.cpp
*
* Modification History:
*
*   Date        Name        Description
*   01/11/02    Ram         Creation.
*   02/12/08    Spieth      Fix errant 'new Object[][]{' insertion
*   02/19/08    Spieth      Removed ICUListResourceBundle dependency
*******************************************************************************
*/

#include <assert.h>
#include "unicode/unistr.h"
#include "reslist.h"
#include "unewdata.h"
#include "unicode/ures.h"
#include "errmsg.h"
#include "filestrm.h"
#include "cstring.h"
#include "unicode/ucnv.h"
#include "genrb.h"
#include "rle.h"
#include "uhash.h"
#include "uresimp.h"
#include "unicode/ustring.h"
#include "unicode/utf8.h"

void res_write_java(struct SResource *res,UErrorCode *status);


static const char copyRight[] =
    "/* \n"
    " *******************************************************************************\n"
    " *\n"
    " *   Copyright (C) International Business Machines\n"
    " *   Corporation and others.  All Rights Reserved.\n"
    " *\n"
    " *******************************************************************************\n"
    " * $" "Source:  $ \n"
    " * $" "Date:  $ \n"
    " * $" "Revision:  $ \n"
    " *******************************************************************************\n"
    " */\n\n";
static const char warningMsg[] =
    "/*********************************************************************\n"
    "######################################################################\n"
    "\n"
    "   WARNING: This file is generated by genrb Version " GENRB_VERSION ".\n"
    "            If you edit this file, please make sure that, the source\n"
    "            of this file (XXXX.txt in LocaleElements_XXXX.java)\n"
    "            is also edited.\n"
    "######################################################################\n"
    " *********************************************************************\n"
    " */\n\n";
static const char* openBrace="{\n";
static const char* closeClass="    };\n"
                              "}\n";

static const char* javaClass =  "import java.util.ListResourceBundle;\n\n"
                                "public class ";

static const char* javaClass1=  " extends ListResourceBundle {\n\n"
                                "    /**\n"
                                "     * Overrides ListResourceBundle \n"
                                "     */\n"
                                "    public final Object[][] getContents() { \n"
                                "          return  contents;\n"
                                "    }\n\n"
                                "    private static Object[][] contents = {\n";
/*static const char* javaClassICU= " extends ListResourceBundle {\n\n"
                                 "    public %s  () {\n"
                                 "          super.contents = data;\n"
                                 "    }\n"
                                 "    static final Object[][] data = new Object[][] { \n";*/
static int tabCount = 3;

static FileStream* out=nullptr;
static struct SRBRoot* srBundle ;
/*static const char* outDir = nullptr;*/

static const char* bName=nullptr;
static const char* pName=nullptr;

static void write_tabs(FileStream* os){
    int i=0;
    for(;i<=tabCount;i++){
        T_FileStream_write(os,"    ",4);
    }
}

#define ZERO 0x30

static const char* enc ="";
static UConverter* conv = nullptr;

static int32_t
uCharsToChars(char *target, int32_t targetLen, const char16_t *source, int32_t sourceLen, UErrorCode *status) {
    int i=0, j=0;
    char str[30]={'\0'};
    while(i<sourceLen){
        if (source[i] == '\n') {
            if (j + 2 < targetLen) {
                uprv_strcat(target, "\\n");
            }
            j += 2;
        }else if(source[i]==0x0D){
            if(j+2<targetLen){
                uprv_strcat(target,"\\f");
            }
            j+=2;
        }else if(source[i] == '"'){
            if(source[i-1]=='\''){
                if(j+2<targetLen){
                    uprv_strcat(target,"\\");
                    target[j+1]= (char)source[i];
                }
                j+=2;
            }else if(source[i-1]!='\\'){

                if(j+2<targetLen){
                    uprv_strcat(target,"\\");
                    target[j+1]= (char)source[i];
                }
                j+=2;
            }else if(source[i-1]=='\\'){
                target[j++]= (char)source[i];
            }
        }else if(source[i]=='\\'){
            if(i+1<sourceLen){
                switch(source[i+1]){
                case ',':
                case '!':
                case '?':
                case '#':
                case '.':
                case '%':
                case '&':
                case ':':
                case ';':
                    if(j+2<targetLen){
                       uprv_strcat(target,"\\\\");
                    }
                    j+=2;
                    break;
                case '"':
                case '\'':
                    if(j+3<targetLen){
                       uprv_strcat(target,"\\\\\\");
                    }
                    j+=3;
                    break;
                default :
                    if(j<targetLen){
                        target[j]=(char)source[i];
                    }
                    j++;
                    break;
                }
            }else{
                if(j<targetLen){
                    uprv_strcat(target,"\\\\");
                }
                j+=2;
            }
        }else if(source[i]>=0x20 && source[i]<0x7F/*ASCII*/){
            if(j<targetLen){
                target[j] = (char) source[i];
            }
            j++;
        }else{
            if(*enc =='\0' || source[i]==0x0000){
                uprv_strcpy(str,"\\u");
                itostr(str+2,source[i],16,4);
                if(j+6<targetLen){
                    uprv_strcat(target,str);
                }
                j+=6;
            }else{
                char dest[30] = {0};
                int retVal=ucnv_fromUChars(conv,dest,30,source+i,1,status);
                if(U_FAILURE(*status)){
                    return 0;
                }
                if(j+retVal<targetLen){
                    uprv_strcat(target,dest);
                }
                j+=retVal;
            }
        }
        i++;
    }
    return j;
}


static uint32_t
strrch(const char* source,uint32_t sourceLen,char find){
    const char* tSourceEnd =source + (sourceLen-1);
    while(tSourceEnd>= source){
        if(*tSourceEnd==find){
            return (uint32_t)(tSourceEnd-source);
        }
        tSourceEnd--;
    }
    return (uint32_t)(tSourceEnd-source);
}

static int32_t getColumnCount(int32_t len){
    int32_t columnCount = 80;
    int32_t maxLines = 3000;
    int32_t adjustedLen = len*5; /* assume that every codepoint is represented in \uXXXX format*/
    /*
     * calculate the number of lines that
     * may be required if column count is 80
     */
    if (maxLines  < (adjustedLen / columnCount) ){
        columnCount = adjustedLen / maxLines;
    }
    return columnCount;
}
static void
str_write_java(const char16_t *src, int32_t srcLen, UBool printEndLine, UErrorCode *status) {

    uint32_t length = srcLen*8;
    uint32_t bufLen = 0;
    uint32_t columnCount;
    char* buf = (char*) malloc(sizeof(char)*length);

    if(buf == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    columnCount = getColumnCount(srcLen);
    memset(buf,0,length);

    bufLen = uCharsToChars(buf,length,src,srcLen,status);
    // buflen accounts for extra bytes added due to multi byte encoding of
    //        non ASCII characters
    if(printEndLine)
        write_tabs(out);

    if(U_FAILURE(*status)){
        uprv_free(buf);
        return;
    }

    if(bufLen+(tabCount*4) > columnCount  ){
        uint32_t len = 0;
        char* current = buf;
        uint32_t add;
        while(len < bufLen){
            add = columnCount-(tabCount*4)-5/* for ", +\n */;
            current = buf +len;
            if (add < (bufLen-len)) {
                uint32_t idx = strrch(current,add,'\\');
                if (idx > add) {
                    idx = add;
                } else {
                    int32_t num =idx-1;
                    uint32_t seqLen;
                    while(num>0){
                        if(current[num]=='\\'){
                            num--;
                        }else{
                            break;
                        }
                    }
                    if ((idx-num)%2==0) {
                        idx--;
                    }
                    seqLen = (current[idx+1]=='u') ? 6 : 2;
                    if ((add-idx) < seqLen) {
                        add = idx + seqLen;
                    }
                }
            }
            T_FileStream_write(out,"\"",1);
            uint32_t byteIndex = 0;
            uint32_t trailBytes = 0;
            if(len+add<bufLen){
                // check the trail bytes to be added to the output line
                while (byteIndex < add) {
                    if (U8_IS_LEAD(*(current + byteIndex))) {
                        trailBytes = U8_COUNT_TRAIL_BYTES(*(current + byteIndex));
                        add += trailBytes;
                    }
                    byteIndex++;
                }
                T_FileStream_write(out,current,add);
                if (len + add < bufLen) {
                    T_FileStream_write(out,"\" +\n",4);
                    write_tabs(out);
                }
            }else{
                T_FileStream_write(out,current,bufLen-len);
            }
            len+=add;
        }
    }else{
        T_FileStream_write(out,"\"",1);
        T_FileStream_write(out, buf,bufLen);
    }
    if(printEndLine){
        T_FileStream_write(out,"\",\n",3);
    }else{
        T_FileStream_write(out,"\"",1);
    }
    uprv_free(buf);
}

/* Writing Functions */
static void
string_write_java(const StringResource *res,UErrorCode *status) {
    (void)res->getKeyString(srBundle);

    str_write_java(res->getBuffer(), res->length(), true, status);
}

static void
array_write_java(const ArrayResource *res, UErrorCode *status) {

    const char* arr ="new String[] { \n";
    struct SResource *current = nullptr;
    UBool allStrings    = true;

    if (U_FAILURE(*status)) {
        return;
    }

    if (res->fCount > 0) {

        current = res->fFirst;
        while(current != nullptr){
            if(!current->isString()){
                allStrings = false;
                break;
            }
            current= current->fNext;
        }

        current = res->fFirst;
        if(allStrings==false){
            const char* object = "new Object[]{\n";
            write_tabs(out);
            T_FileStream_write(out, object, (int32_t)uprv_strlen(object));
            tabCount++;
        }else{
            write_tabs(out);
            T_FileStream_write(out, arr, (int32_t)uprv_strlen(arr));
            tabCount++;
        }
        while (current != nullptr) {
            /*if(current->isString()){
                write_tabs(out);
            }*/
            res_write_java(current, status);
            if(U_FAILURE(*status)){
                return;
            }
            current = current->fNext;
        }
        T_FileStream_write(out,"\n",1);

        tabCount--;
        write_tabs(out);
        T_FileStream_write(out,"},\n",3);

    } else {
        write_tabs(out);
        T_FileStream_write(out,arr,(int32_t)uprv_strlen(arr));
        write_tabs(out);
        T_FileStream_write(out,"},\n",3);
    }
}

static void
intvector_write_java(const IntVectorResource *res, UErrorCode * /*status*/) {
    uint32_t i = 0;
    const char* intArr = "new int[] {\n";
    /* const char* intC   = "new Integer(";   */
    const char* stringArr = "new String[]{\n";
    const char *resname = res->getKeyString(srBundle);
    char buf[100];
    int len =0;
    buf[0]=0;
    write_tabs(out);

    if(resname != nullptr && uprv_strcmp(resname,"DateTimeElements")==0){
        T_FileStream_write(out, stringArr, (int32_t)uprv_strlen(stringArr));
        tabCount++;
        for(i = 0; i<res->fCount; i++) {
            write_tabs(out);
            len=itostr(buf,res->fArray[i],10,0);
            T_FileStream_write(out,"\"",1);
            T_FileStream_write(out,buf,len);
            T_FileStream_write(out,"\",",2);
            T_FileStream_write(out,"\n",1);
        }
    }else{
        T_FileStream_write(out, intArr, (int32_t)uprv_strlen(intArr));
        tabCount++;
        for(i = 0; i<res->fCount; i++) {
            write_tabs(out);
            /* T_FileStream_write(out, intC, (int32_t)uprv_strlen(intC)); */
            len=itostr(buf,res->fArray[i],10,0);
            T_FileStream_write(out,buf,len);
            /* T_FileStream_write(out,"),",2);  */
            /* T_FileStream_write(out,"\n",1);  */
            T_FileStream_write(out,",\n",2);
        }
    }
    tabCount--;
    write_tabs(out);
    T_FileStream_write(out,"},\n",3);
}

static void
int_write_java(const IntResource *res, UErrorCode * /*status*/) {
    const char* intC   =  "new Integer(";
    char buf[100];
    int len =0;
    buf[0]=0;

    /* write the binary data */
    write_tabs(out);
    T_FileStream_write(out, intC, (int32_t)uprv_strlen(intC));
    len=itostr(buf, res->fValue, 10, 0);
    T_FileStream_write(out,buf,len);
    T_FileStream_write(out,"),\n",3 );

}

static void
bytes_write_java(const BinaryResource *res, UErrorCode * /*status*/) {
	const char* type  = "new byte[] {";
	const char* byteDecl = "%i, ";
    char byteBuffer[100] = { 0 };
	uint8_t*  byteArray = nullptr;
    int byteIterator = 0;
    int32_t srcLen=res->fLength;
    if(srcLen>0 )
	{
        byteArray = res->fData;

        write_tabs(out);
        T_FileStream_write(out, type, (int32_t)uprv_strlen(type));
        T_FileStream_write(out, "\n", 1);
        tabCount++;

		for (;byteIterator<srcLen;byteIterator++)
		{
            if (byteIterator%16 == 0)
			{
			    write_tabs(out);
			}

			if (byteArray[byteIterator] < 128)
			{
                snprintf(byteBuffer, sizeof(byteBuffer), byteDecl, byteArray[byteIterator]);
			}
			else
			{
                snprintf(byteBuffer, sizeof(byteBuffer), byteDecl, (byteArray[byteIterator]-256));
			}

            T_FileStream_write(out, byteBuffer, (int32_t)uprv_strlen(byteBuffer));

			if (byteIterator%16 == 15)
			{
                T_FileStream_write(out, "\n", 1);
			}

		}

        if (((byteIterator-1)%16) != 15)
		{
            T_FileStream_write(out, "\n", 1);
	    }

		tabCount--;
        write_tabs(out);
		T_FileStream_write(out, "},\n", 3);

	}
	else
    {
		/* Empty array */
        write_tabs(out);
        T_FileStream_write(out,type,(int32_t)uprv_strlen(type));
		T_FileStream_write(out,"},\n",3);
    }

}

static UBool start = true;

static void
table_write_java(const TableResource *res, UErrorCode *status) {
    uint32_t  i         = 0;
    struct SResource *current = nullptr;
    const char* obj = "new Object[][]{\n";

    if (U_FAILURE(*status)) {
        return ;
    }

    if (res->fCount > 0) {
        if(start==false){
            write_tabs(out);
            T_FileStream_write(out, obj, (int32_t)uprv_strlen(obj));
            tabCount++;
        }
        start = false;
        current = res->fFirst;
        i       = 0;


        while (current != nullptr) {
            const char *currentKeyString = current->getKeyString(srBundle);

            assert(i < res->fCount);
            write_tabs(out);

            T_FileStream_write(out, openBrace, 2);


            tabCount++;

            write_tabs(out);
            if(currentKeyString != nullptr) {
                T_FileStream_write(out, "\"", 1);
                T_FileStream_write(out, currentKeyString,
                                   (int32_t)uprv_strlen(currentKeyString));
                T_FileStream_write(out, "\",\n", 2);

                T_FileStream_write(out, "\n", 1);
            }
            res_write_java(current, status);
            if(U_FAILURE(*status)){
                return;
            }
            i++;
            current = current->fNext;
            tabCount--;
            write_tabs(out);
            T_FileStream_write(out, "},\n", 3);
        }
        if(tabCount>4){
            tabCount--;
            write_tabs(out);
            T_FileStream_write(out, "},\n", 3);
        }

    } else {
        write_tabs(out);
        T_FileStream_write(out,obj,(int32_t)uprv_strlen(obj));

        write_tabs(out);
        T_FileStream_write(out,"},\n",3);

    }

}

void
res_write_java(struct SResource *res,UErrorCode *status) {

    if (U_FAILURE(*status)) {
        return ;
    }

    if (res != nullptr) {
        switch (res->fType) {
        case URES_STRING:
             string_write_java    (static_cast<const StringResource *>(res), status);
             return;
        case URES_ALIAS:
             printf("Encountered unsupported resource type %d of alias\n", res->fType);
             *status = U_UNSUPPORTED_ERROR;
			 return;
        case URES_INT_VECTOR:
             intvector_write_java (static_cast<const IntVectorResource *>(res), status);
             return;
        case URES_BINARY:
             bytes_write_java     (static_cast<const BinaryResource *>(res), status);
             return;
        case URES_INT:
             int_write_java       (static_cast<const IntResource *>(res), status);
             return;
        case URES_ARRAY:
             array_write_java     (static_cast<const ArrayResource *>(res), status);
             return;
        case URES_TABLE:
             table_write_java     (static_cast<const TableResource *>(res), status);
             return;
        default:
            break;
        }
    }

    *status = U_INTERNAL_PROGRAM_ERROR;
}

void
bundle_write_java(struct SRBRoot *bundle, const char *outputDir,const char* outputEnc,
                  char *writtenFilename, int writtenFilenameLen,
                  const char* packageName, const char* bundleName,
                  UErrorCode *status) {

    char fileName[256] = {'\0'};
    char className[256]={'\0'};
    /*char constructor[1000] = { 0 };*/
    /*UBool j1 =false;*/
    /*outDir = outputDir;*/

    start = true;                        /* Reset the start indicator*/

    bName = (bundleName==nullptr) ? "LocaleElements" : bundleName;
    pName = (packageName==nullptr)? "com.ibm.icu.impl.data" : packageName;

    uprv_strcpy(className, bName);
    srBundle = bundle;
    if(uprv_strcmp(srBundle->fLocale,"root")!=0){
        uprv_strcat(className,"_");
        uprv_strcat(className,srBundle->fLocale);
    }
    if(outputDir){
        uprv_strcpy(fileName, outputDir);
        if(outputDir[uprv_strlen(outputDir)-1] !=U_FILE_SEP_CHAR){
            uprv_strcat(fileName,U_FILE_SEP_STRING);
        }
        uprv_strcat(fileName,className);
        uprv_strcat(fileName,".java");
    }else{
        uprv_strcat(fileName,className);
        uprv_strcat(fileName,".java");
    }

    if (writtenFilename) {
        uprv_strncpy(writtenFilename, fileName, writtenFilenameLen);
    }

    if (U_FAILURE(*status)) {
        return;
    }

    out= T_FileStream_open(fileName,"w");

    if(out==nullptr){
        *status = U_FILE_ACCESS_ERROR;
        return;
    }
    if(getIncludeCopyright()){
        T_FileStream_write(out, copyRight, (int32_t)uprv_strlen(copyRight));
        T_FileStream_write(out, warningMsg, (int32_t)uprv_strlen(warningMsg));
    }
    T_FileStream_write(out,"package ",(int32_t)uprv_strlen("package "));
    T_FileStream_write(out,pName,(int32_t)uprv_strlen(pName));
    T_FileStream_write(out,";\n\n",3);
    T_FileStream_write(out, javaClass, (int32_t)uprv_strlen(javaClass));
    T_FileStream_write(out, className, (int32_t)uprv_strlen(className));
	T_FileStream_write(out, javaClass1, (int32_t)uprv_strlen(javaClass1));

    /* if(j1){
          T_FileStream_write(out, javaClass1, (int32_t)uprv_strlen(javaClass1));
       }else{
           sprintf(constructor,javaClassICU,className);
           T_FileStream_write(out, constructor, (int32_t)uprv_strlen(constructor));
       }
    */

    if(outputEnc && *outputEnc!='\0'){
        /* store the output encoding */
        enc = outputEnc;
        conv=ucnv_open(enc,status);
        if(U_FAILURE(*status)){
            return;
        }
    }
    res_write_java(bundle->fRoot, status);

    T_FileStream_write(out, closeClass, (int32_t)uprv_strlen(closeClass));

    T_FileStream_close(out);

    ucnv_close(conv);
}
