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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "webcfg_log.h"
#include "webcfg_metadata.h"
#include "webcfg_multipart.h"

/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
#define MAXCHAR 1024
/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
typedef struct SubDocSupportMap
{
    char name[256];//portforwarding or wlan
    char support[8];//true or false;
    struct SubDocSupportMap *next;
}SubDocSupportMap_t;

/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
static char * supported_bits = NULL;
static char * supported_version = NULL;
SubDocSupportMap_t *g_sdInfoHead = NULL;
SubDocSupportMap_t *g_sdInfoTail = NULL;
/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/
void displaystruct();
SubDocSupportMap_t * get_global_sdInfoHead(void);
SubDocSupportMap_t * get_global_sdInfoTail(void);
/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/


void initWebcfgProperties(char * filename)
{
	FILE *fp = NULL;
	char str[MAXCHAR] = {'\0'};
	//For WEBCONFIG_SUBDOC_MAP parsing
	int line_index =0;
	char *p;
	char *token;

	WebcfgDebug("webcfg properties file path is %s\n", filename);
	fp = fopen(filename,"r");

	if (fp == NULL)
	{
		WebcfgError("Failed to open file %s\n", filename);
		return;
	}

	while (fgets(str, MAXCHAR, fp) != NULL)
	{
		char * value = NULL;

		if(NULL != (value = strstr(str,"WEBCONFIG_SUPPORTED_DOCS_BIT=")))
		{
			WebcfgDebug("The value stored is %s\n", str);
			value = value + strlen("WEBCONFIG_SUPPORTED_DOCS_BIT=");
			value[strlen(value)-1] = '\0';
			setsupportedDocs(value);
                        value = NULL;
		}

		if(NULL != (value =strstr(str,"WEBCONFIG_DOC_SCHEMA_VERSION")))
		{
			WebcfgDebug("The value stored is %s\n", str);
			value = value + strlen("WEBCONFIG_DOC_SCHEMA_VERSION=");
			value[strlen(value)-1] = '\0';
			setsupportedVersion(value);
			value = NULL;
		}

		if(strncmp(str,"WEBCONFIG_SUBDOC_MAP",strlen("WEBCONFIG_SUBDOC_MAP")) ==0)
		{
			
			p = str;
			char *tok = NULL;
			WebcfgDebug("The value of tok is %s\n", tok);
			tok = strtok_r(p, " =",&p);
			token = strtok_r(p,",",&p);
			while(token!= NULL)
			{

				char subdoc[100];
				char *subtoken;
				SubDocSupportMap_t *sdInfo = NULL;
				strncpy(subdoc,token,(sizeof(subdoc)-1));
				puts(subdoc);
				sdInfo = (SubDocSupportMap_t *)malloc(sizeof(SubDocSupportMap_t));
				if( sdInfo==NULL )
				{
					fclose(fp);
					WebcfgError("Unable to allocate memory");
					return;
				}
				memset(sdInfo, 0, sizeof(SubDocSupportMap_t));

				subtoken = strtok(subdoc,":");//portforwarding or lan

				if(subtoken == NULL)
				{
					fclose(fp);
					WEBCFG_FREE(sdInfo);
					return;
				}
				
				strncpy(sdInfo->name,subtoken,(sizeof(sdInfo->name)-1));
				subtoken = strtok(NULL,":");//skip 1st value
				subtoken = strtok(NULL,":");//true or false				
				strncpy(sdInfo->support,subtoken,(sizeof(sdInfo->support)-1));
				token =strtok_r(p,",",&p);
				sdInfo->next = NULL;

				if(g_sdInfoHead == NULL)
				{
					g_sdInfoHead = sdInfo;
					g_sdInfoTail = NULL;
				}
				else
				{
					SubDocSupportMap_t *temp =NULL;
					if(g_sdInfoTail == NULL)
					{
						temp = get_global_sdInfoHead();
						temp->next=sdInfo;
						g_sdInfoTail = sdInfo;
					}
					else
					{
						temp = get_global_sdInfoTail();
						temp->next = sdInfo;
						g_sdInfoTail = sdInfo;
					}
				}

			}
			line_index++;

		}
	}
	fclose(fp);

	if(g_sdInfoHead != NULL)
	{
		displaystruct();
	}
}

void setsupportedDocs( char * value)
{
	if(value != NULL)
	{
		supported_bits = strdup(value);
	}
	else
	{
		supported_bits = NULL;
	}
}

void setsupportedVersion( char * value)
{
	if(value != NULL)
	{
		supported_version = strdup(value);
	}
	else
	{
		supported_version = NULL;
	}
}

char * getsupportedDocs()
{
	WebcfgDebug("The value in supportedbits get is %s\n",supported_bits);
	return supported_bits;
}

char * getsupportedVersion()
{
      WebcfgDebug("The value in supportedversion get is %s\n",supported_version);
      return supported_version;
}

WEBCFG_STATUS isSubDocSupported(char *subDoc)
{

	SubDocSupportMap_t *sd = NULL;
	
	sd = get_global_sdInfoHead();

	while(sd != NULL)
	{
		if(strncmp(sd->name, subDoc, strlen(subDoc)) == 0)
		{
			WebcfgDebug("The subdoc %s is present\n",sd->name);
			if(strncmp(sd->support, "true", strlen("true")) == 0)
			{
				WebcfgInfo("%s is supported\n",subDoc);
				return WEBCFG_SUCCESS;
				
			}
			else
			{
				WebcfgInfo("%s is not supported\n",subDoc);
				return WEBCFG_FAILURE;
			}
		}
		sd = sd->next;
		
	}
	WebcfgError("Supported doc bit not found for %s\n",subDoc);
	return WEBCFG_FAILURE;
}

/*----------------------------------------------------------------------------*/
/*                             Internal functions                             */
/*----------------------------------------------------------------------------*/
SubDocSupportMap_t * get_global_sdInfoHead(void)
{
    SubDocSupportMap_t *tmp = NULL;
    tmp = g_sdInfoHead;
    return tmp;
}

SubDocSupportMap_t * get_global_sdInfoTail(void)
{
    SubDocSupportMap_t *tmp = NULL;
    tmp = g_sdInfoTail;
    return tmp;
}

void displaystruct()
{
	SubDocSupportMap_t *temp =NULL;
	temp = get_global_sdInfoHead();
	while(temp != NULL)
	{
		WebcfgDebug(" %s %s\n",temp->name,temp->support);
		temp=temp->next;
	}
}
