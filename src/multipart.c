/*
 * Copyright 2020 Comcast Cable Communications Management, LLC
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

#include "multipart.h"
#include "webcfgdoc.h"
#include "webcfgparam.h"
#include "portmappingdoc.h"
#include "portmappingparam.h"
#include "portmappingpack.h"
#include <uuid/uuid.h>
#include <string.h>
#include <stdlib.h>
/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
#define METADATA_MAP_SIZE                2
#define MAX_HEADER_LEN			4096
#define ETAG_HEADER 		       "Etag:"
#define CURL_TIMEOUT_SEC	   25L
#define CA_CERT_PATH 		   "/etc/ssl/certs/ca-certificates.crt"
#define WEBPA_READ_HEADER             "/etc/parodus/parodus_read_file.sh"
#define WEBPA_CREATE_HEADER           "/etc/parodus/parodus_create_file.sh"
/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
struct token_data {
    size_t size;
    char* data;
};

/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
//static char g_interface[32]={'\0'};
static char g_ETAG[64]={'\0'};
char webpa_auth_token[4096]={'\0'};
/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/
size_t write_callback_fn(void *buffer, size_t size, size_t nmemb, struct token_data *data);
size_t header_callback(char *buffer, size_t size, size_t nitems);
void stripSpaces(char *str, char **final_str);
void createCurlheader( struct curl_slist *list, struct curl_slist **header_list);
void print_multipart(char *ptr, int no_of_bytes, int part_no);
void parse_multipart(char *ptr, int no_of_bytes, multipartdocs_t *m, int *no_of_subdocbytes);
void multipart_destroy( multipart_t *m );
void xxd( const void *buffer, const size_t length );
static int alterMap( char * buf );
size_t appendEncodedData( void **appendData, void *encodedBuffer, size_t encodedSize, void *metadataPack, size_t metadataSize );
/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/
/*
* @brief Initialize curl object with required options. create configData using libcurl.
* @param[out] configData 
* @param[in] len total configData size
* @param[in] r_count Number of curl retries on ipv4 and ipv6 mode during failure
* @return returns 0 if success, otherwise failed to fetch auth token and will be retried.
*/
int webcfg_http_request(char *webConfigURL, char **configData, int r_count, long *code, char *interface, char** sub_buff, int *sub_len)
{
	CURL *curl;
	CURLcode res;
	CURLcode time_res;
	struct curl_slist *list = NULL;
	struct curl_slist *headers_list = NULL;
	int rv=1,count =0;
	double total;
	long response_code = 0;
	//char *interface = NULL;
	char *ct = NULL;
	char *boundary = NULL;
	char *str=NULL;
	char *line_boundary = NULL;
	char *last_line_boundary = NULL;
	char *str_body = NULL;
	multipart_t *mp = NULL;
	int subdocbytes =0;

	//char *webConfigURL= NULL;
	int content_res=0;
	struct token_data data;
	data.size = 0;
	void * dataVal = NULL;
	curl = curl_easy_init();
	if(curl)
	{
		//this memory will be dynamically grown by write call back fn as required
		data.data = (char *) malloc(sizeof(char) * 1);
		if(NULL == data.data)
		{
			printf("Failed to allocate memory.\n");
			return rv;
		}
		data.data[0] = '\0';
		createCurlheader(list, &headers_list);
		//getConfigURL(index, &configURL);

		if(webConfigURL !=NULL)
		{
			printf("webconfig root ConfigURL is %s\n", webConfigURL);
			curl_easy_setopt(curl, CURLOPT_URL, webConfigURL );
		}
		else
		{
			printf("Failed to get webconfig root configURL\n");
		}
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_SEC);
		/**if(strlen(g_interface) == 0)
		{
			//get_webCfg_interface(&interface);
			if(interface !=NULL)
		        {
		               strncpy(g_interface, interface, sizeof(g_interface)-1);
		               printf("g_interface copied is %s\n", g_interface);
		               WEBCFG_FREE(interface);
		        }
		}
		printf("g_interface fetched is %s\n", g_interface);**/
		if(strlen(interface) > 0)
		{
			printf("setting interface %s\n", interface);
			curl_easy_setopt(curl, CURLOPT_INTERFACE, interface);
		}

		// set callback for writing received data
		dataVal = &data;
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_fn);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, dataVal);

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);

		//printf("Set CURLOPT_HEADERFUNCTION option\n");
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);

		// setting curl resolve option as default mode.
		//If any failure, retry with v4 first and then v6 mode. 
		if(r_count == 1)
		{
			printf("curl Ip resolve option set as V4 mode\n");
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		}
		else if(r_count == 2)
		{
			printf("curl Ip resolve option set as V6 mode\n");
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
		}
		else
		{
			printf("curl Ip resolve option set as default mode\n");
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
		}
		curl_easy_setopt(curl, CURLOPT_CAINFO, CA_CERT_PATH);
		// disconnect if it is failed to validate server's cert 
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		// Verify the certificate's name against host 
  		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		// To use TLS version 1.2 or later 
  		curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
		// To follow HTTP 3xx redirections
  		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		// Perform the request, res will get the return code 
		res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		printf("webConfig curl response %d http_code %ld\n", res, response_code);
		*code = response_code;
		time_res = curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total);
		if(time_res == 0)
		{
			printf("curl response Time: %.1f seconds\n", total);
		}
		curl_slist_free_all(headers_list);
		WEBCFG_FREE(webConfigURL);
		if(res != 0)
		{
			printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		}
		else
		{
                        printf("checking content type\n");
			content_res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
			printf("ct is %s, content_res is %d\n", ct, content_res);
			// fetch boundary
			str = strtok(ct,";");
			str = strtok(NULL, ";");			
			boundary= strtok(str,"=");
			boundary= strtok(NULL,"=");
			printf( "boundary %s\n", boundary );
			int boundary_len =0;
			if(boundary !=NULL)
			{
				boundary_len= strlen(boundary);
			}

			line_boundary  = (char *)malloc(sizeof(char) * (boundary_len +5));
			snprintf(line_boundary,boundary_len+5,"--%s\r\n",boundary);
			printf( "line_boundary %s, len %ld\n", line_boundary, strlen(line_boundary) );

			last_line_boundary  = (char *)malloc(sizeof(char) * (boundary_len + 5));
			snprintf(last_line_boundary,boundary_len+5,"--%s--",boundary);
			printf( "last_line_boundary %s, len %ld\n", last_line_boundary, strlen(last_line_boundary) );

			// Use --boundary to split
			str_body = malloc(sizeof(char) * data.size + 1);
			str_body = memcpy(str_body, data.data, data.size + 1);
			int num_of_parts = 0;
			char *ptr_lb=str_body;
			char *ptr_lb1=str_body;
			char *ptr_count = str_body;
			int index1=0, index2 =0 ;
                        webcfgparam_t *pm;

			/*For Subdocs count*/
			while((ptr_count - str_body) < (int)data.size )
			{
				if(0 == memcmp(ptr_count, last_line_boundary, strlen(last_line_boundary)))
				{
					num_of_parts++;
					break;
				}
				else if(0 == memcmp(ptr_count, line_boundary, strlen(line_boundary)))
				{
					num_of_parts++;
				}
				ptr_count++;
			}
			printf("Size of the docs is :%d\n", (num_of_parts-1));
			/*For Subdocs count*/

			mp = (multipart_t *) malloc (sizeof(multipart_t));
			mp->entries_count = (size_t)num_of_parts;
			mp->entries = (multipartdocs_t *) malloc(sizeof(multipartdocs_t )*(mp->entries_count-1) );
			memset( mp->entries, 0, sizeof(multipartdocs_t)*(mp->entries_count-1));
			///Scanning each lines with \n as delimiter
			while((ptr_lb - str_body) < (int)data.size)
			{
				if(0 == memcmp(ptr_lb, last_line_boundary, strlen(last_line_boundary)))
				{
					printf("last line boundary \n");
					break;
				}
				if (0 == memcmp(ptr_lb, "-", 1) && 0 == memcmp(ptr_lb, line_boundary, strlen(line_boundary)))
				{
					ptr_lb = ptr_lb+(strlen(line_boundary));
					num_of_parts = 1;
					while(0 != num_of_parts % 2)
					{
						ptr_lb = memchr(ptr_lb, '\n', data.size - (ptr_lb - str_body));
						// printf("printing newline: %ld\n",ptr_lb-str_body);
						ptr_lb1 = memchr(ptr_lb+1, '\n', data.size - (ptr_lb - str_body));
						// printf("printing newline2: %ld\n",ptr_lb1-str_body);
						if(0 != memcmp(ptr_lb1-1, "\r",1 )){
						ptr_lb1 = memchr(ptr_lb1+1, '\n', data.size - (ptr_lb - str_body));
						}
						index2 = ptr_lb1-str_body;
						index1 = ptr_lb-str_body;
						parse_multipart(str_body+index1+1,index2 - index1 - 2, &mp->entries[count],&subdocbytes);
						ptr_lb++;

						if(0 == memcmp(ptr_lb, last_line_boundary, strlen(last_line_boundary)))
						{
							printf("last line boundary inside \n");
							break;
						}
						if(0 == memcmp(ptr_lb1+1, "-", 1) && 0 == memcmp(ptr_lb1+1, line_boundary, strlen(line_boundary)))
						{
							printf(" line boundary inside \n");
							num_of_parts++;
							count++;
						}
					}
				}
				else
				{
					ptr_lb++;
				}
			}
			printf("Data size is : %d\n",(int)data.size);

			for(size_t m = 0 ; m<(mp->entries_count-1); m++)
			{
				printf("mp->entries[%ld].name_space %s\n", m, mp->entries[m].name_space);
				printf("mp->entries[%ld].etag %s\n" ,m,  mp->entries[m].etag);
				printf("mp->entries[%ld].data %s\n" ,m,  mp->entries[m].data);

				printf("subdocbytes is %d\n", subdocbytes);

				//process one subdoc
				*sub_buff = mp->entries[m].data;
				*sub_len = subdocbytes;
                                //mp->version = strndup(mp->entries[m].etag,strlen(mp->entries[m].etag));
                                //mp-> 
				printf("*sub_len %d\n", *sub_len);
			}
                        
                        //decode root doc
			//printf("subLen is %d\n", (int)sub_len);
			printf("--------------decode root doc-------------\n");
			pm = webcfgparam_convert( *sub_buff, *sub_len+1 );
			printf("blob_size is %d\n", pm->entries[0].value_size);
		        //err = errno;
			//printf( "errno: %s\n", webcfgparam_strerror(err) );
			//CU_ASSERT_FATAL( NULL != pm );
			//CU_ASSERT_FATAL( NULL != pm->entries );
			for(int i = 0; i < (int)pm->entries_count ; i++)
			{
				printf("pm->entries[%d].name %s\n", i, pm->entries[i].name);
				printf("pm->entries[%d].value %s\n" , i, pm->entries[i].value);
				printf("pm->entries[%d].type %d\n", i, pm->entries[i].type);
			}

                        appenddoc_t *appenddata = NULL;
                        size_t appenddocPackSize = -1;
                        size_t embeddeddocPackSize = -1;
                        void *appenddocdata = NULL;
                        void *embeddeddocdata = NULL;
 
                        msgpack_zone mempool;
			msgpack_object deserialized;
			msgpack_unpack_return unpack_ret;

                        appenddata = (appenddoc_t *) malloc(sizeof(appenddoc_t ));
                        if(appenddata != NULL)
                        {   
                            memset(appenddata, 0, sizeof(appenddoc_t));
 
                            appenddata->version = strdup ("32566738870767626680659770");
                            appenddata->transaction_id = strdup("portforwarding-1");
                        }

                        printf("Append Doc \n");
                        appenddocPackSize = portmap_pack_appenddoc(appenddata, &appenddocdata);
                        printf("appenddocPackSize is %ld\n", appenddocPackSize);
	                printf("data packed is %s\n", (char*)appenddocdata);
                        

                        printf("---------------------------------------------------------------\n");
                        embeddeddocPackSize = appendEncodedData(&embeddeddocdata, (void *)pm->entries[0].value, (size_t)pm->entries[0].value_size, appenddocdata, appenddocPackSize);
                         printf("embeddeddocPackSize is %ld\n", embeddeddocPackSize);
	                printf("data packed is %s\n", (char*)embeddeddocdata);

               //Start of msgpack decoding just to verify
		printf("----Start of msgpack decoding----\n");
		msgpack_zone_init(&mempool, 2048);
		unpack_ret = msgpack_unpack(embeddeddocdata, embeddeddocPackSize, NULL, &mempool, &deserialized);
		printf("unpack_ret is %d\n",unpack_ret);
		switch(unpack_ret)
		{
			case MSGPACK_UNPACK_SUCCESS:
				printf("MSGPACK_UNPACK_SUCCESS :%d\n",unpack_ret);
				printf("\nmsgpack decoded data is:");
				msgpack_object_print(stdout, deserialized);
			break;
			case MSGPACK_UNPACK_EXTRA_BYTES:
				printf("MSGPACK_UNPACK_EXTRA_BYTES :%d\n",unpack_ret);
			break;
			case MSGPACK_UNPACK_CONTINUE:
				printf("MSGPACK_UNPACK_CONTINUE :%d\n",unpack_ret);
			break;
			case MSGPACK_UNPACK_PARSE_ERROR:
				printf("MSGPACK_UNPACK_PARSE_ERROR :%d\n",unpack_ret);
			break;
			case MSGPACK_UNPACK_NOMEM_ERROR:
				printf("MSGPACK_UNPACK_NOMEM_ERROR :%d\n",unpack_ret);
			break;
			default:
				printf("Message Pack decode failed with error: %d\n", unpack_ret);
		}

		msgpack_zone_destroy(&mempool);
		printf("----End of msgpack decoding----\n");
                printf("------PARSING--------------------\n");
                
                portmappingdoc_t *rpm;
		//printf("--------------decode blob-------------\n");
               // printf("pm->entries[0].value_size is : %d\n",pm->entries[0].value_size);
		rpm = portmappingdoc_convert( embeddeddocdata, embeddeddocPackSize );
		//err = errno;
		//printf( "errno: %s\n", portmappingdoc_strerror(err) );
		//CU_ASSERT_FATAL( NULL != rpm );
		//CU_ASSERT_FATAL( NULL != rpm->entries );
		printf("rpm->entries_count is %ld\n", rpm->entries_count);

		for(int i = 0; i < (int)rpm->entries_count ; i++)
		{
			printf("rpm->entries[%d].InternalClient %s\n", i, rpm->entries[i].internal_client);
			printf("rpm->entries[%d].ExternalPortEndRange %s\n" , i, rpm->entries[i].external_port_end_range);
			printf("rpm->entries[%d].Enable %s\n", i, rpm->entries[i].enable?"true":"false");
			printf("rpm->entries[%d].Protocol %s\n", i, rpm->entries[i].protocol);
			printf("rpm->entries[%d].Description %s\n", i, rpm->entries[i].description);
			printf("rpm->entries[%d].external_port %s\n", i, rpm->entries[i].external_port);
		}

		portmappingdoc_destroy( rpm );
                printf("------END OF PARSING--------------------\n");
		//End of msgpack decoding
			


                       //printf("Number of sub docs %d\n",((num_of_parts-2)/6));
                        

                        //printf("Subdoc ....................\n");
                         //xxd(*sub_buff, (size_t)subdocbytes);
                         printf("Append ....................\n");
                         xxd(appenddocdata, appenddocPackSize);
                      //   printf("embedded ....................\n");
                        // xxd(embeddeddocdata, embeddeddocPackSize);



			*configData=str_body;
		}
                //multipart_destroy(mp);
                free(mp);
		WEBCFG_FREE(data.data);
		curl_easy_cleanup(curl);
		rv=0;
	}
	else
	{
		printf("curl init failure\n");
	}
	return rv;
}

/*----------------------------------------------------------------------------*/
/*                             Internal functions                             */
/*----------------------------------------------------------------------------*/
/* @brief callback function for writing libcurl received data
 * @param[in] buffer curl delivered data which need to be saved.
 * @param[in] size size is always 1
 * @param[in] nmemb size of delivered data
 * @param[out] data curl response data saved.
*/
size_t write_callback_fn(void *buffer, size_t size, size_t nmemb, struct token_data *data)
{
    size_t index = data->size;
    size_t n = (size * nmemb);
    char* tmp; 
    data->size += (size * nmemb);

    tmp = realloc(data->data, data->size + 1); // +1 for '\0' 

    if(tmp) {
        data->data = tmp;
    } else {
        if(data->data) {
            free(data->data);
        }
        printf("Failed to allocate memory for data\n");
        return 0;
    }
    memcpy((data->data + index), buffer, n);
    data->data[data->size] = '\0';
    printf("size * nmemb is %lu\n", size * nmemb);
    return size * nmemb;
}

/* @brief callback function to extract response header data.
*/
size_t header_callback(char *buffer, size_t size, size_t nitems)
{
	size_t etag_len = 0;
	char* header_value = NULL;
	char* final_header = NULL;
	char header_str[64] = {'\0'};

	etag_len = strlen(ETAG_HEADER);
	if( nitems > etag_len )
	{
		if( strncasecmp(ETAG_HEADER, buffer, etag_len) == 0 )
		{
			header_value = strtok(buffer, ":");
			while( header_value != NULL )
			{
				header_value = strtok(NULL, ":");
				if(header_value !=NULL)
				{
					strncpy(header_str, header_value, sizeof(header_str)-1);
					stripSpaces(header_str, &final_header);

					strncpy(g_ETAG, final_header, sizeof(g_ETAG)-1);
				}
			}
		}
	}
	printf("size %lu\n", size);
	return nitems;
}

//To strip all spaces , new line & carriage return characters from header output
void stripSpaces(char *str, char **final_str)
{
	int i=0, j=0;

	for(i=0;str[i]!='\0';++i)
	{
		if(str[i]!=' ')
		{
			if(str[i]!='\n')
			{
				if(str[i]!='\r')
				{
					str[j++]=str[i];
				}
			}
		}
	}
	str[j]='\0';
	*final_str = str;
}

int readFromFile(char *filename, char **data, int *len)
{
	FILE *fp;
	int ch_count = 0;
	fp = fopen(filename, "r+");
	if (fp == NULL)
	{
		printf("Failed to open file %s\n", filename);
		return 0;
	}
	fseek(fp, 0, SEEK_END);
	ch_count = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	*data = (char *) malloc(sizeof(char) * (ch_count + 1));
	fread(*data, 1, ch_count-1,fp);
        
	*len = ch_count;
	(*data)[ch_count] ='\0';
	fclose(fp);
	return 1;
}

/* @brief Function to create curl header options
 * @param[in] list temp curl header list
 * @param[in] device status value
 * @param[out] header_list output curl header list
*/
void createCurlheader( struct curl_slist *list, struct curl_slist **header_list)
{
	char *auth_header = NULL;
	char *token = NULL;
	int len = 0;

	printf("Start of createCurlheader\n");

	// Read token from file
	readFromFile("/tmp/webcfg_token", &token, &len );
	strncpy(webpa_auth_token, token, len);
	if(strlen(webpa_auth_token)==0)
	{
		printf(">>>>>>><token> is NULL.. hardcode token here. for test purpose.\n");
	}
	
	auth_header = (char *) malloc(sizeof(char)*MAX_HEADER_LEN);
	if(auth_header !=NULL)
	{
		snprintf(auth_header, MAX_HEADER_LEN, "Authorization:Bearer %s", (0 < strlen(webpa_auth_token) ? webpa_auth_token : NULL));
		list = curl_slist_append(list, auth_header);
		WEBCFG_FREE(auth_header);
	}
	
	list = curl_slist_append(list, "Accept: application/msgpack");

	*header_list = list;
}

void multipart_destroy( multipart_t *m )
{
    if( NULL != m ) {
     /*   size_t i;
        for( i = 0; i < m->entries_count; i++ ) {
            if( NULL != m->entries[i].name_space ) {
                printf("name_space %ld",i);
                free( m->entries[i].name_space );
            }
	    if( NULL != m->entries[i].etag ) {
                printf("etag %ld",i);
                free( m->entries[i].etag );
            }
             if( NULL != m->entries[i].data ) {
                printf("data %ld",i);
                free( m->entries[i].data );
            }
        }
        if( NULL != m->entries ) {
            printf("entries %ld",i);
            free( m->entries );
        }*/
        free( m );
    }
}


int writeToFile(char *filename, char *data, int len)
{
	FILE *fp;
	fp = fopen(filename , "w+");
	if (fp == NULL)
	{
		printf("Failed to open file %s\n", filename );
		return 0;
	}
	if(data !=NULL)
	{
		fwrite(data, len, 1, fp);
		fclose(fp);
		return 1;
	}
	else
	{
		printf("WriteToFile failed, Data is NULL\n");
		return 0;
	}
}

void print_multipart(char *ptr, int no_of_bytes, int part_no)
{
	printf("########################################\n");
	int i = 0;
	char *filename = malloc(sizeof(char)*6);
	snprintf(filename,6,"%s%d","part",part_no);
	while(i <= no_of_bytes)
	{
		putc(*(ptr+i),stdout);
		i++;
	}
	printf("########################################\n");
	writeToFile(filename,ptr,no_of_bytes);
}

void parse_multipart(char *ptr, int no_of_bytes, multipartdocs_t *m, int *no_of_subdocbytes)
{
	void * mulsubdoc;

	/*for storing respective values */
        if(0 == strncasecmp(ptr,"Namespace",strlen("Namespace")-1)){
             m->name_space = strndup(ptr+(strlen("Namespace: ")),no_of_bytes-((strlen("Namespace: "))));
             printf("The Namespace is %s\n",m->name_space);
          }
          else if(0 == strncasecmp(ptr,"Etag",strlen("Etag")-1)){
             m->etag = strndup(ptr+(strlen("Etag: ")),no_of_bytes-((strlen("Etag: "))));
             printf("The Etag is %s\n",m->etag);
          }
          else if(strstr(ptr,"parameters")){
             m->data = ptr;
             mulsubdoc = (void *) ptr;
             printf("The paramters is %s\n",m->data);
             webcfgparam_convert( mulsubdoc, no_of_bytes );
	     *no_of_subdocbytes = no_of_bytes;
	     printf("*no_of_subdocbytes is %d\n", *no_of_subdocbytes);
	}
}


/**
 * @brief alterMap function to change MAP size of encoded msgpack object.
 *
 * @param[in] encodedBuffer msgpack object
 * @param[out] return 0 in success or less than 1 in failure case
 */

static int alterMap( char * buf )
{
    //Extract 1st byte from binary stream which holds type and map size
    unsigned char *byte = ( unsigned char * )( &( buf[0] ) ) ;
    int mapSize;
    printf("First byte in hex : %x\n", 0xff & *byte );
    //Calculate map size
    mapSize = ( 0xff & *byte ) % 0x10;
    printf("Map size is :%d\n", mapSize );

    if( mapSize == 15 ) {
        printf("Msgpack Map (fixmap) is already at its MAX size i.e. 15\n" );
        return -1;
    }

    *byte = *byte + METADATA_MAP_SIZE;
    mapSize = ( 0xff & *byte ) % 0x10;
    printf("New Map size : %d\n", mapSize );
    printf("First byte in hex : %x\n", 0xff & *byte );
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

size_t appendEncodedData( void **appendData, void *encodedBuffer, size_t encodedSize, void *metadataPack, size_t metadataSize )
{
    //Allocate size for final buffer
    //printf("before appenddata malloc");
    *appendData = ( void * )malloc( sizeof( char * ) * ( encodedSize + metadataSize ) );
       // printf("after appenddata malloc");
	if(*appendData != NULL)
	{
		memcpy( *appendData, encodedBuffer, encodedSize );
		//Append 2nd encoded buf with 1st encoded buf
		memcpy( *appendData + ( encodedSize ), metadataPack, metadataSize );
		//Alter MAP
		int ret = alterMap( ( char * ) * appendData );

		if( ret ) {
		    return -1;
		}
		return ( encodedSize + metadataSize );
	}
	else
	{
		printf("Memory allocation failed\n" );
	}
    return -1;
}


void xxd( const void *buffer, const size_t length )
{
    const char hex[17] = "0123456789abcdef";
    const char *data = (char *) buffer;
    const char *end = &data[length];
    char output[70];
    size_t line = 0;

    if( (NULL == buffer) || (0 == length) ) {
        return;
    }

    while( data < end ) {
        int shiftCount;
        size_t i;
        char *text_ptr = &output[51];
        char *ptr = output;

        /* Output the '00000000:' portion */
        for (shiftCount=28; shiftCount >= 0; shiftCount -= 4) {
            *ptr++ = hex[(line >> shiftCount) & 0x0F];
        }
        *ptr++ = ':';
        *ptr++ = ' ';

        for( i = 0; i < 16; i++ ) {
            if( data < end ) {
                *ptr++ = hex[(0x0f & (*data >> 4))];
                *ptr++ = hex[(0x0f & (*data))];
                if( (' ' <= *data) && (*data <= '~') ) {
                    *text_ptr++ = *data;
                } else {
                    *text_ptr++ = '.';
                }
                data++;
            } else {
                *ptr++ = ' ';
                *ptr++ = ' ';
                *text_ptr++ = ' ';
            }
            if( 0x01 == (0x01 & i) ) {
                *ptr++ = ' ';
            }
        }
        line += 16;
        *ptr = ' ';

        *text_ptr++ = '\n';
        *text_ptr = '\0';

        puts( output );
    }
}
