//-----------------------------------------------------------------------------
// Copyright (C) 2018 Merlok
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// EMV json logic
//-----------------------------------------------------------------------------

#include "emvjson.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "util.h"
#include "ui.h"
#include "proxmark3.h"
#include "emv_tags.h"

static const ApplicationDataElm ApplicationData[] = {
{0x82,    "AIP"},
{0x94,    "AFL"},

{0x5A,    "PAN"},
{0x5F34,  "PANSeqNo"},
{0x5F24,  "ExpirationDate"},
{0x5F25,  "EffectiveDate"},
{0x5F28,  "IssuerCountryCode"},

{0x50,    "ApplicationLabel"},
{0x9F08,  "VersionNumber"},
{0x9F42,  "CurrencyCode"},
{0x5F2D,  "LanguagePreference"},
{0x87,    "PriorityIndicator"},
{0x9F36,  "ATC"}, //Application Transaction Counter

{0x5F20,  "CardholderName"},

{0x9F38,  "PDOL"},
{0x8C,    "CDOL1"},
{0x8D,    "CDOL2"},

{0x9F07,  "AUC"},   // Application Usage Control
{0x9F6C,  "CTQ"},
{0x8E,    "CVMList"},
{0x9F0D,  "IACDefault"},
{0x9F0E,  "IACDeny"},
{0x9F0F,  "IACOnline"},

{0x8F,    "CertificationAuthorityPublicKeyIndex"},
{0x9F32,  "IssuerPublicKeyExponent"},
{0x92,    "IssuerPublicKeyRemainder"},
{0x90,    "IssuerPublicKeyCertificate"},
{0x9F47,  "ICCPublicKeyExponent"},
{0x9F46,  "ICCPublicKeyCertificate"},

{0x00,    "end..."}
};
int ApplicationDataLen = sizeof(ApplicationData) / sizeof(ApplicationDataElm);

char* GetApplicationDataName(tlv_tag_t tag) {
	for (int i = 0; i < ApplicationDataLen; i++)
		if (ApplicationData[i].Tag == tag)
			return ApplicationData[i].Name;
		
	return NULL;
}

int JsonSaveJsonObject(json_t *root, char *path, json_t *value) {
	json_error_t error;

	if (strlen(path) < 1)
		return 1;
	
	if (path[0] == '$') {
		if (json_path_set(root, path, value, 0, &error)) {
			PrintAndLog("ERROR: can't set json path: ", error.text);
			return 2;
		} else {
			return 0;
		}
	} else {
		return json_object_set_new(root, path, value);
	}
}

int JsonSaveInt(json_t *root, char *path, int value) {
	return JsonSaveJsonObject(root, path, json_integer(value));
}

int JsonSaveStr(json_t *root, char *path, char *value) {
	return JsonSaveJsonObject(root, path, json_string(value));
};

int JsonSaveBufAsHexCompact(json_t *elm, char *path, uint8_t *data, size_t datalen) {
	char * msg = sprint_hex_inrow(data, datalen);
	if (msg && strlen(msg) && msg[strlen(msg) - 1] == ' ')
		msg[strlen(msg) - 1] = '\0';

	return JsonSaveStr(elm, path, msg);
}

int JsonSaveBufAsHex(json_t *elm, char *path, uint8_t *data, size_t datalen) {
	char * msg = sprint_hex(data, datalen);
	if (msg && strlen(msg) && msg[strlen(msg) - 1] == ' ')
		msg[strlen(msg) - 1] = '\0';

	return JsonSaveStr(elm, path, msg);
}

int JsonSaveHex(json_t *elm, char *path, uint64_t data, int datalen) {
	uint8_t bdata[8] = {0};
	int len = 0;
	if (!datalen) {
		for (uint64_t u = 0xffffffffffffffff; u; u = u << 8) {
			if (!(data & u)) {
				break;
			}
			len++;
		}
		if (!len)
			len = 1;
	} else {
		len = datalen;
	}	
	num_to_bytes(data, len, bdata);
	
	return JsonSaveBufAsHex(elm, path, bdata, len);
}

int JsonSaveTLVValue(json_t *root, char *path, struct tlvdb *tlvdbelm) {
	const struct tlv *tlvelm = tlvdb_get_tlv(tlvdbelm);
	if (tlvelm)
		return JsonSaveBufAsHex(root, path, (uint8_t *)tlvelm->value, tlvelm->len);
	else
		return 1;	
}

int JsonSaveTLVElm(json_t *elm, char *path, struct tlv *tlvelm, bool saveName, bool saveValue, bool saveAppDataLink) {
	json_error_t error;

	if (strlen(path) < 1 || !tlvelm)
		return 1;
	
	if (path[0] == '$') {

		json_t *obj = json_path_get(elm, path);
		if (!obj) {
			obj = json_object();
		
			if (json_is_array(elm)) {
				if (json_array_append_new(elm, obj)) {
					PrintAndLog("ERROR: can't append array: %s", path);
					return 2;
				}
			} else {
				if (json_path_set(elm, path, obj, 0, &error)) {
					PrintAndLog("ERROR: can't set json path: ", error.text);
					return 2;
				}
			}
		}
		
		if (saveAppDataLink) {
			char * AppDataName = GetApplicationDataName(tlvelm->tag);
			if (AppDataName)
				JsonSaveStr(obj, "appdata", AppDataName);
		} else {		
			char * name = emv_get_tag_name(tlvelm);
			if (saveName && name && strlen(name) > 0 && strncmp(name, "Unknown", 7))
				JsonSaveStr(obj, "name", emv_get_tag_name(tlvelm));
			JsonSaveHex(obj, "tag", tlvelm->tag, 0);
			if (saveValue) {
				JsonSaveHex(obj, "length", tlvelm->len, 0);
				JsonSaveBufAsHex(obj, "value", (uint8_t *)tlvelm->value, tlvelm->len);
			};
		}
	}

	return 0;
}

int JsonSaveTLVTreeElm(json_t *elm, char *path, struct tlvdb *tlvdbelm, bool saveName, bool saveValue, bool saveAppDataLink) {
	return JsonSaveTLVElm(elm, path, (struct tlv *)tlvdb_get_tlv(tlvdbelm), saveName, saveValue, saveAppDataLink);
}

int JsonSaveTLVTree(json_t *root, json_t *elm, char *path, struct tlvdb *tlvdbelm) {
	struct tlvdb *tlvp = tlvdbelm;
	while (tlvp) {
		const struct tlv * tlvpelm = tlvdb_get_tlv(tlvp);
		char * AppDataName = NULL;
		if (tlvpelm)
			AppDataName = GetApplicationDataName(tlvpelm->tag);
		
		if (AppDataName) {
			char appdatalink[200] = {0};
			sprintf(appdatalink, "$.ApplicationData.%s", AppDataName);
			JsonSaveBufAsHex(root, appdatalink, (uint8_t *)tlvpelm->value, tlvpelm->len);
		}
		
		json_t *pelm = json_path_get(elm, path);
		if (pelm && json_is_array(pelm)) {
			json_t *appendelm = json_object();
			json_array_append_new(pelm, appendelm);
			JsonSaveTLVTreeElm(appendelm, "$", tlvp, !AppDataName, !tlvdb_elm_get_children(tlvp), AppDataName);
			pelm = appendelm;
		} else {
			JsonSaveTLVTreeElm(elm, path, tlvp, !AppDataName, !tlvdb_elm_get_children(tlvp), AppDataName);
			pelm = json_path_get(elm, path);
		}
		
		if (tlvdb_elm_get_children(tlvp)) {
			// get path element
			if(!pelm)
				return 1;
			
			// check childs element and add it if not found
			json_t *chjson = json_path_get(pelm, "$.Childs");
			if (!chjson) {
				json_object_set_new(pelm, "Childs", json_array());
				
				chjson = json_path_get(pelm, "$.Childs");
			}
			
			// check
			if (!json_is_array(chjson)) {
				PrintAndLog("E->Internal logic error. `$.Childs` is not an array.");
				break;
			}

			// Recursion
			JsonSaveTLVTree(root, chjson, "$", tlvdb_elm_get_children(tlvp));
		}

		tlvp = tlvdb_elm_get_next(tlvp);
	}
	return 0;
}

bool HexToBuffer(const char *errormsg, const char *hexvalue, uint8_t * buffer, size_t maxbufferlen, size_t *bufferlen) {
	int buflen = 0;	
	
	switch(param_gethex_to_eol(hexvalue, 0, buffer, maxbufferlen, &buflen)) {
	case 1:
		PrintAndLog("%s Invalid HEX value.", errormsg);
		return false;
	case 2:
		PrintAndLog("%s Hex value too large.", errormsg);
		return false;
	case 3:
		PrintAndLog("%s Hex value must have even number of digits.", errormsg);
		return false;
	}
	
	if (buflen > maxbufferlen) {
		PrintAndLog("%s HEX length (%d) more than %d", errormsg, *bufferlen, maxbufferlen);
		return false;
	}
	
	*bufferlen = buflen;
	
	return true;
}

int JsonLoadStr(json_t *root, char *path, char *value) {
	if (!value)
		return 1;

	json_t *jelm = json_path_get((const json_t *)root, path);
	if (!jelm || !json_is_string(jelm))
		return 2;
	
	const char * strval = json_string_value(jelm);
	if (!strval)
		return 1;
	
	memcpy(value, strval, strlen(strval));
	
	return 0;
}

int JsonLoadBufAsHex(json_t *elm, char *path, uint8_t *data, size_t maxbufferlen, size_t *datalen) {
	if (datalen)
		*datalen = 0;
	
	json_t *jelm = json_path_get((const json_t *)elm, path);
	if (!jelm || !json_is_string(jelm))
		return 1;
	
	if (!HexToBuffer("ERROR load", json_string_value(jelm), data, maxbufferlen, datalen))
		return 2;
	
	return 0;
};

bool ParamLoadFromJson(struct tlvdb *tlv) {
	json_t *root;
	json_error_t error;

	if (!tlv) {
		PrintAndLog("ERROR load params: tlv tree is NULL.");
		return false; 
	}

	// current path + file name
	const char *relfname = "emv/defparams.json"; 
	char fname[strlen(get_my_executable_directory()) + strlen(relfname) + 1];
	strcpy(fname, get_my_executable_directory());
	strcat(fname, relfname);

	root = json_load_file(fname, 0, &error);
	if (!root) {
		PrintAndLog("Load params: json error on line %d: %s", error.line, error.text);
		return false; 
	}
	
	if (!json_is_array(root)) {
		PrintAndLog("Load params: Invalid json format. root must be array.");
		return false; 
	}
	
	PrintAndLog("Load params: json(%d) OK", json_array_size(root));
	
	for(int i = 0; i < json_array_size(root); i++) {
		json_t *data, *jtag, *jlength, *jvalue;

		data = json_array_get(root, i);
		if(!json_is_object(data))
		{
			PrintAndLog("Load params: data [%d] is not an object", i + 1);
			json_decref(root);
			return false;
		}
		
		jtag = json_object_get(data, "tag");
		if(!json_is_string(jtag))
		{
			PrintAndLog("Load params: data [%d] tag is not a string", i + 1);
			json_decref(root);
			return false;
		}
		const char *tlvTag = json_string_value(jtag);

		jvalue = json_object_get(data, "value");
		if(!json_is_string(jvalue))
		{
			PrintAndLog("Load params: data [%d] value is not a string", i + 1);
			json_decref(root);
			return false;
		}
		const char *tlvValue = json_string_value(jvalue);

		jlength = json_object_get(data, "length");
		if(!json_is_number(jlength))
		{
			PrintAndLog("Load params: data [%d] length is not a number", i + 1);
			json_decref(root);
			return false;
		}
		
		int tlvLength = json_integer_value(jlength);
		if (tlvLength > 250) {
			PrintAndLog("Load params: data [%d] length more than 250", i + 1);
			json_decref(root);
			return false;
		}
		
		PrintAndLog("TLV param: %s[%d]=%s", tlvTag, tlvLength, tlvValue);
		uint8_t buf[251] = {0};
		size_t buflen = 0;
		
		if (!HexToBuffer("TLV Error type:", tlvTag, buf, 4, &buflen)) { 
			json_decref(root);
			return false;
		}
		tlv_tag_t tag = 0;
		for (int i = 0; i < buflen; i++) {
			tag = (tag << 8) | buf[i];
		}	
		
		if (!HexToBuffer("TLV Error value:", tlvValue, buf, sizeof(buf) - 1, &buflen)) {
			json_decref(root);
			return false;
		}
		
		if (buflen != tlvLength) {
			PrintAndLog("Load params: data [%d] length of HEX must(%d) be identical to length in TLV param(%d)", i + 1, buflen, tlvLength);
			json_decref(root);
			return false;
		}
		
		tlvdb_change_or_add_node(tlv, tag, tlvLength, (const unsigned char *)buf);		
	}

	json_decref(root);

	return true;
}

