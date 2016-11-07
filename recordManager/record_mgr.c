//
// Created by Sowmya Parameshwara on 11/3/16.
//

#include "record_mgr.h"
#include "buffer_mgr.h"
#include "storage_mgr.h"
#include <string.h>
#include <stdlib.h>
#include <tgmath.h>

typedef struct RMTableMgmtData
{
    int noOfTuples;
    int firstFreePageNumber;
    int recordSize;
    BM_PageHandle pageHandle;
    BM_BufferPool bufferPool;
} RMTableMgmtData;

RC initRecordManager (void *mgmtData){
    return RC_OK;
}

RC shutdownRecordManager (){
    return RC_OK;
}

RC createTable (char *name, Schema *schema){
    SM_FileHandle fh;
    RC rc;
    if((rc = createPageFile(name)) != RC_OK){
        return rc;
    }
    if((rc = openPageFile(name,&fh)) != RC_OK){
        return rc;
    }
    int i;
    char data[PAGE_SIZE];
    char *metaData = data;
    memset(metaData, 0, PAGE_SIZE);


    *(int*)metaData = 0; // Number of tuples
    metaData = metaData+ sizeof(int); //increment char pointer

    *(int*)metaData = 2; // First free page is 1 because page 0 is reserved for metadata
    metaData = metaData + sizeof(int); //increment char pointer

    *(int*)metaData = getRecordSize(schema);
    metaData = metaData + sizeof(int); //increment char pointer

    *(int*)metaData = schema->numAttr; //set num of attributes
    metaData = metaData + sizeof(int);

    for(i=0; i<schema->numAttr; i++)
    {

        strncpy(metaData, schema->attrNames[i], 20);	// set Attribute Name,assuming max field name is 20
        metaData = metaData + 20;

        *(int*)metaData = (int)schema->dataTypes[i];	// Set the Data Types of Attribute
        metaData = metaData + sizeof(int);

        *(int*)metaData = (int) schema->typeLength[i];	// Set the typeLength of Attribute
        metaData = metaData + sizeof(int);

    }
    for(i=0; i<schema->keySize; i++) {
        *(int*)metaData = schema->keyAttrs[i]; // set keys for the schema
        metaData = metaData + sizeof(int);
    }

    if((rc=writeBlock(1, &fh, data))!=RC_OK){ // Write all meta data info To 0th page of file
        return rc;
    }

    if((rc=closePageFile(&fh))!=RC_OK){
        return rc;
    }
    return RC_OK;
}


RC openTable (RM_TableData *rel, char *name) {
    SM_PageHandle  metadata;
    RC rc;
    int i;
    Schema *schema = (Schema *) malloc(sizeof(Schema));


    RMTableMgmtData *tableMgmtData = (RMTableMgmtData *) malloc(sizeof(RMTableMgmtData));
    rel->mgmtData = tableMgmtData;
    rel->name = name;
    if ((rc = initBufferPool(&tableMgmtData->bufferPool, name, 10, RS_LRU, NULL)) != RC_OK) {
        return rc;
    }
    if ((rc = pinPage(&tableMgmtData->bufferPool, &tableMgmtData->pageHandle, 1)) != RC_OK) { // pinning the 0th page which has meta data
       return rc;
    }
    metadata = (char*) tableMgmtData->pageHandle.data;
    tableMgmtData->noOfTuples= *(int*)metadata;
    metadata = metadata + sizeof(int);
    tableMgmtData->firstFreePageNumber= *(int*)metadata;
    metadata = metadata + sizeof(int);
    tableMgmtData->recordSize=*(int *)metadata;
    metadata = metadata + sizeof(int);
    schema->numAttr = *(int*)metadata;
    metadata = metadata + sizeof(int);

    schema->attrNames= (char**) malloc( sizeof(char*)*schema->numAttr);
    schema->dataTypes= (DataType*) malloc( sizeof(DataType)*schema->numAttr);
    schema->typeLength= (int*) malloc( sizeof(int)*schema->numAttr);
    for(i=0; i < schema->numAttr; i++) {
        schema->attrNames[i]= (char*) malloc(20); //20 is max field length
    }

    for(i=0; i < schema->numAttr; i++) {

        strncpy(schema->attrNames[i], metadata, 20);
        metadata = metadata + 20;

        schema->dataTypes[i]= *(int*)metadata;
        metadata = metadata + sizeof(int);

        schema->typeLength[i]= *(int*)metadata;
        metadata = metadata + sizeof(int);
    }
    schema->keySize = *(int*)metadata;
    metadata = metadata + sizeof(int);

    for(i=0; i<schema->keySize; i++) {
        schema->keyAttrs[i] = *(int*)metadata;
        metadata = metadata + sizeof(int);
    }
    rel->schema = schema;

    if((rc = unpinPage(&tableMgmtData->bufferPool, &tableMgmtData->pageHandle)) != RC_OK){
        return rc;
    }
    return RC_OK;
}

RC closeTable (RM_TableData *rel){
    RC rc;
    RMTableMgmtData* rmTableMgmtData;
    SM_FileHandle *fileHandle;
    rmTableMgmtData = rel->mgmtData;
    writeBlock(1, &fileHandle, );
    if((rc=shutdownBufferPool(&rmTableMgmtData->bufferPool)) != RC_OK){
        return rc;
    }
    rel->mgmtData = NULL;
    return RC_OK;
}

RC deleteTable (char *name){
    RC rc;
    if((rc = destroyPageFile(name)) != RC_OK){
        return rc;
    }
    return RC_OK;
}

int getNumTuples (RM_TableData *rel){

    RMTableMgmtData *rmTableMgmtData;
    rmTableMgmtData = rel->mgmtData;
    return rmTableMgmtData->noOfTuples;
}

RC insertRecord (RM_TableData *rel, Record *record){

    RMTableMgmtData *rmTableMgmtData;
    rmTableMgmtData = rel->mgmtData;

    char *data, *slotAddress;
    int recordSize = getRecordSize(rel->schema);	// record size of particular Record

    RID *rid = &record->id;	// set rid from current Record
    rid->page = rmTableMgmtData->firstFreePageNumber;
    rid->slot = -1;

    pinPage(&rmTableMgmtData->bufferPool, &rmTableMgmtData->pageHandle, rid->page);
    data = rmTableMgmtData->pageHandle.data;

    int i;
    int totalSlots = floor(PAGE_SIZE/recordSize);
    for (i = 0; i < totalSlots; i++) {
        if (data[i * recordSize] != '#'){
            printf("data[ slot num : %d] contains: %c  \n\n", i, data[i * recordSize]);
            rid->slot = i;
            break;
        }
    }

    while(rid->slot == -1){

        unpinPage(&rmTableMgmtData->bufferPool, &rmTableMgmtData->pageHandle);
        rid->page++;	// increment page number
        pinPage(&rmTableMgmtData->bufferPool, &rmTableMgmtData->pageHandle, rid->page);
        for (i = 0; i < totalSlots; i++) {
            if (data[i * recordSize] != '#'){
                printf("data[ slot num : %d] contains: %c  \n\n", i, data[i * recordSize]);
                rid->slot = i;
                break;
            }
        }
    }

    slotAddress = data;
    markDirty(&rmTableMgmtData->bufferPool, &rmTableMgmtData->pageHandle);

    slotAddress = slotAddress + rid->slot * recordSize;
    *slotAddress = '#';
    slotAddress++;
    memcpy(slotAddress, record->data + 1, recordSize - 1);

    unpinPage(&rmTableMgmtData->bufferPool, &rmTableMgmtData->pageHandle);
    rmTableMgmtData->noOfTuples++;

    return RC_OK;
}

RC deleteRecord (RM_TableData *rel, RID id){
    return RC_OK;
}

RC updateRecord (RM_TableData *rel, Record *record){
    return RC_OK;
}

RC getRecord (RM_TableData *rel, RID id, Record *record){
    return RC_OK;
}

RC startScan (RM_TableData *rel, RM_ScanHandle *scan, Expr *cond){
    return RC_OK;
}

RC next (RM_ScanHandle *scan, Record *record){
    return RC_OK;
}

RC closeScan (RM_ScanHandle *scan){
    return RC_OK;
}

extern int getRecordSize (Schema *schema){

    int size = 0, i;
    for(i=0; i<schema->numAttr; ++i){
        switch (schema->dataTypes[i]){
            case DT_STRING:
                size = size + schema->typeLength[i];
                break;
            case DT_INT:
                size = size + sizeof(int);
                break;
            case DT_FLOAT:
                size = size + sizeof(float);
                break;
            case DT_BOOL:
                size = size + sizeof(bool);
                break;
            default:
                break;
        }
    }
    return size;
}

Schema *createSchema (int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys){

    Schema *schema = malloc(sizeof(Schema));
    schema->numAttr = numAttr;
    schema->attrNames = attrNames;
    schema->dataTypes = dataTypes;
    schema->typeLength = typeLength;
    schema->keySize = keySize;
    schema->keyAttrs = keys;
    return schema;
}

RC freeSchema (Schema *schema){

    free(schema);
    return RC_OK;
}

RC createRecord (Record **record, Schema *schema){

    int recordSize = getRecordSize(schema);
    Record *newRecord = (Record*) malloc(sizeof(Record));
    newRecord->data= (char*) malloc(recordSize); // Allocate memory for data of record

    char *data = newRecord->data; // set char pointer to data of record
    *data = '0'; // set '0' because record is still empty

    data = data + sizeof(char); // Move pointer by 1 byte
    *data = '\0'; // set null value to record after tombstone

    newRecord->id.page= -1; // page number is not fixed for empty record which is in memory
    newRecord->id.slot= -1; // slot number is not fixed for empty record which is in memory

    *record = newRecord; // set tempRecord to Record
    return RC_OK;
}

RC freeRecord (Record *record){

    free(record);
    return RC_OK;
}

RC getAttr (Record *record, Schema *schema, int attrNum, Value **value){
    return RC_OK;
}

RC setAttr (Record *record, Schema *schema, int attrNum, Value *value){
    return RC_OK;
}


