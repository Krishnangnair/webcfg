/*
 * Copyright 2019 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <base64.h>
#include <msgpack.h>

#include "webcfg_log.h"
#include "webcfg_helpers.h"
#include "webcfg_blob.h"
#include "webcfg_db.h"

/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
#define METADATA_MAP_SIZE                3
#define APPEND_FILE                     "/tmp/append_data.bin"
/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
struct webcfg_token {
    const char *name;
    size_t length;
};
/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
/* none */

/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
/*                             Internal functions                             */
/*----------------------------------------------------------------------------*/
static void __msgpack_pack_string( msgpack_packer *pk, const void *string, size_t n );
static void __msgpack_pack_string_nvp( msgpack_packer *pk,
                                       const struct webcfg_token *token,
                                       const char *val );

static int alterMapData( char * buf );

static void __msgpack_pack_string( msgpack_packer *pk, const void *string, size_t n )
{
    msgpack_pack_str( pk, n );
    msgpack_pack_str_body( pk, string, n );
}

static void __msgpack_pack_string_nvp( msgpack_packer *pk,
                                       const struct webcfg_token *token,
                                       const char *val )
{
    if( ( NULL != token ) && ( NULL != val ) )
    {
        __msgpack_pack_string( pk, token->name, token->length );
        __msgpack_pack_string( pk, val, strlen( val ) );
    }
}

ssize_t webcfg_pack_appenddoc(const appenddoc_t *appenddocData,void **data)
{
    size_t rv = -1;

    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    msgpack_sbuffer_init( &sbuf );
    msgpack_packer_init( &pk, &sbuf, msgpack_sbuffer_write );
    msgpack_zone mempool;
    msgpack_object deserialized;

    if( appenddocData != NULL )
    {
        struct webcfg_token APPENDDOC_MAP_SUBDOC_NAME;

        APPENDDOC_MAP_SUBDOC_NAME.name = "subdoc_name";
        APPENDDOC_MAP_SUBDOC_NAME.length = strlen( "subdoc_name" );
        __msgpack_pack_string_nvp( &pk, &APPENDDOC_MAP_SUBDOC_NAME, appenddocData->subdoc_name );

        struct webcfg_token APPENDDOC_MAP_VERSION;
             
        APPENDDOC_MAP_VERSION.name = "version";
        APPENDDOC_MAP_VERSION.length = strlen( "version" );
        __msgpack_pack_string( &pk, APPENDDOC_MAP_VERSION.name, APPENDDOC_MAP_VERSION.length );
        msgpack_pack_uint32(&pk,appenddocData->version);

        struct webcfg_token APPENDDOC_MAP_TRANSACTION_ID;
             
        APPENDDOC_MAP_TRANSACTION_ID.name = "transaction_id";
        APPENDDOC_MAP_TRANSACTION_ID.length = strlen( "transaction_id" );
        __msgpack_pack_string( &pk, APPENDDOC_MAP_TRANSACTION_ID.name, APPENDDOC_MAP_TRANSACTION_ID.length );
        msgpack_pack_uint16(&pk, appenddocData->transaction_id);
    }
    else 
    {    
        WebcfgInfo("parameters is NULL\n" );
        return rv;
    } 

    if( sbuf.data ) 
    {
        *data = ( char * ) malloc( sizeof( char ) * sbuf.size );
	memset(*data,0,sbuf.size);

        if( NULL != *data ) 
        {
            memcpy( *data, sbuf.data, sbuf.size );
	    WebcfgInfo("sbuf.size of appenddoc is %zu\n", sbuf.size);
            rv = sbuf.size;
        }
    }

    msgpack_zone_init(&mempool, 2048);

    msgpack_unpack(sbuf.data, sbuf.size, NULL, &mempool, &deserialized);
    msgpack_object_print(stdout, deserialized);

    msgpack_zone_destroy(&mempool);

    msgpack_sbuffer_destroy( &sbuf );
    return rv;   
}


/**
 * @brief alterMapData function to change MAP size of encoded msgpack object.
 *
 * @param[in] encodedBuffer msgpack object
 * @param[out] return 0 in success or less than 1 in failure case
 */

static int alterMapData( char *buf )
{
    //Extract 1st byte from binary stream which holds type and map size
    unsigned char *byte = ( unsigned char * )( &( buf[0] ) ) ;
    int mapSize;
    WebcfgInfo("First byte in hex : %x\n", 0xff & *byte );
    //Calculate map size
    mapSize = ( 0xff & *byte ) % 0x10;
    WebcfgInfo("Map size is :%d\n", mapSize );

    if( mapSize == 15 )
    {
        WebcfgInfo("Msgpack Map (fixmap) is already at its MAX size i.e. 15\n" );
        return -1;
    }

    *byte = *byte + METADATA_MAP_SIZE;
    mapSize = ( 0xff & *byte ) % 0x10;
    WebcfgInfo("New Map size : %d\n", mapSize );
    WebcfgInfo("First byte in hex : %x\n", 0xff & *byte );
    //Update 1st byte with new MAP size
    buf[0] = *byte;
    return 0;
}

/**
 * @brief appendEncodedData function to append two encoded buffer and change MAP size accordingly.
 * 
 * @note appendEncodedData function allocates memory for buffer, caller needs to free the buffer(appendData)in
 * both success or failure case. use wrp_free_struct() for free
 *
 * @param[in] encodedBuffer msgpack object (first buffer)
 * @param[in] encodedSize is size of first buffer
 * @param[in] metadataPack msgpack object (second buffer)
 * @param[in] metadataSize is size of second buffer
 * @param[out] appendData final encoded buffer after append
 * @return  appended total buffer size or less than 1 in failure case
 */

size_t appendEncodedData( void *appendData, void *encodedBuffer, size_t encodedSize, void *metadataPack, size_t metadataSize )
{
	WebcfgInfo("Inside appendEncodedData function\n" );
	//Allocate size for final buffer
	if(appendData != NULL)
	{
		memcpy( appendData, encodedBuffer, encodedSize );
		WebcfgInfo("appendEncodedData total length %zu\n", ( encodedSize + metadataSize + 1));
		//Append 2nd encoded buf with 1st encoded buf
		memcpy( appendData + ( encodedSize )+1, metadataPack, metadataSize );
		//Alter MAP
		WebcfgInfo("Before alterMapData\n" );
		int ret = alterMapData( ( char *) (appendData) );
		WebcfgInfo("After alterMapData\n" );

		if( ret ) {
		    return -1;
		}
		return ( encodedSize + metadataSize +1 );
	}
	else
	{
		WebcfgInfo("Memory allocation failed\n" );
	}
    return -1;
}

char * webcfg_appendeddoc(char * subdoc_name, uint32_t version, char * blob_data, size_t blob_size)
{
    appenddoc_t *appenddata = NULL;
    size_t appenddocPackSize = -1;
    size_t embeddeddocPackSize = -1;
    void *appenddocdata = NULL;
    void *embeddeddocdata = NULL;
    char *finaldocdata = NULL;
    char *fileName = NULL;

    appenddata = (appenddoc_t *) malloc(sizeof(appenddoc_t ));
    if(appenddata != NULL)
    {   
        memset(appenddata, 0, sizeof(appenddoc_t));
	
	WebcfgInfo("The version is : %lu\n",(long)version);
        appenddata->subdoc_name = strdup(subdoc_name);
        appenddata->version = version;
        appenddata->transaction_id = generateTransactionId(1001,3000);
    }

    WebcfgInfo("Append Doc \n");
    appenddocPackSize = webcfg_pack_appenddoc(appenddata, &appenddocdata);
    WebcfgInfo("appenddocPackSize is %zu\n", appenddocPackSize);
    WebcfgDebug("data packed is %s\n", (char*)appenddocdata);
 
    free(appenddata->subdoc_name);
    free(appenddata);

    WebcfgInfo("---------------------------------------------------------------\n");
    embeddeddocdata = ( void * )malloc( sizeof( char * ) * ( blob_size + appenddocPackSize + 1) );
    memset(embeddeddocdata,0, ( blob_size + appenddocPackSize + 1));
    embeddeddocPackSize = appendEncodedData(embeddeddocdata, (void *)blob_data, blob_size, appenddocdata, appenddocPackSize);
    WebcfgInfo("embeddeddocPackSize is %zu\n", embeddeddocPackSize);
    WebcfgInfo("The embedded doc data is %s\n",(char*)embeddeddocdata);

    fileName = malloc(sizeof(char) * 32);
    snprintf(fileName,32,"/tmp/%s.bin",subdoc_name);
    writeToFileData(fileName, (char*)embeddeddocdata, embeddeddocPackSize);
    finaldocdata = base64blobencoder((char *)embeddeddocdata, embeddeddocPackSize);
    
    WebcfgInfo("The encoded append doc is %s\n",finaldocdata);
    snprintf(fileName,32,"/tmp/b64_%s.txt",subdoc_name);
    writeToFileData(fileName, (char*)embeddeddocdata, embeddeddocPackSize);
    return finaldocdata;
}

uint16_t generateTransactionId(int min, int max)
{
    srand(time(0));
    return (uint16_t)((rand() % 
           (max - min + 1)) + min);
}

int writeToFileData(char *db_file_path, char *data, size_t size)
{
	FILE *fp;
	fp = fopen(db_file_path , "w+");
	if (fp == NULL)
	{
		WebcfgError("Failed to open file in db %s\n", db_file_path );
		return 0;
	}
	if(data !=NULL)
	{
		fwrite(data, size, 1, fp);
		fclose(fp);
		return 1;
	}
	else
	{
		WebcfgError("WriteToJson failed, Data is NULL\n");
		fclose(fp);
		return 0;
	}
}
