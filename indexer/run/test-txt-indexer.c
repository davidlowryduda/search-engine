#undef NDEBUG /* make sure assert() works */
#include <assert.h>

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>

#include "wstring/wstring.h"
#include "txt-seg/txt-seg.h"
#include "term-index/term-index.h"
#include "keyval-db/keyval-db.h"

#define  LEX_PREFIX(_name) txt ## _name
#include "lex.h"

#include "lex-slice.h"

#include "filesys.h"
#include "config.h"

static void *term_index;
keyval_db_t  keyval_db;

static LIST_IT_CALLBK(get_term_pos)
{
	LIST_OBJ(struct term_list_node, t, ln);
	P_CAST(s, struct lex_slice, pa_extra);

	wchar_t *tmp = mbstr2wstr(s->mb_str); /* first wstr2mbstr() */
	const size_t slice_len = wstr_len(tmp);
	const size_t slice_bytes = sizeof(wchar_t) * (slice_len + 1);
	wchar_t *slice = malloc(slice_bytes);

	char *mb_term = wstr2mbstr(t->term); /* second wstr2mbstr() */
	const size_t term_bytes = strlen(mb_term);

	//uint32_t i;
	uint32_t slice_seg_bytes;
	wchar_t save;
	uint32_t file_offset;

	memcpy(slice, tmp, slice_bytes);
	save = slice[t->begin_pos];
	slice[t->begin_pos] = L'\0';

	printf("term `%s': ", mb_term);
	slice_seg_bytes = strlen(wstr2mbstr(slice));
	file_offset = s->begin + slice_seg_bytes;

	slice[t->begin_pos] = save;

	printf("%u[%lu]\n", file_offset, term_bytes);

//	printf("%u <= %lu\n", t->end_pos, wstr_len(slice));
//	printf("%u <= %u\n", slice_seg_bytes, s->offset);
	assert(t->end_pos <= wstr_len(slice));
	assert(term_bytes <= s->offset);
	
	free(slice);

//	/* add term for inverted-index */
//	term_index_doc_add(term_index, mb_term);
	LIST_GO_OVER;
}

LIST_DEF_FREE_FUN(list_release, struct term_list_node,
                  ln, free(p));

extern void handle_math(struct lex_slice *slice)
{
	printf("math: %s (len=%u)\n", slice->mb_str, slice->offset);
}

static void slice_eng_to_lower(struct lex_slice *slice)
{
	for(size_t i = 0; i < slice->offset; i++)
		slice->mb_str[i] = tolower(slice->mb_str[i]);
}

extern void handle_text(struct lex_slice *slice)
{
	// printf("text: %s (len=%u)\n", slice->mb_str, slice->offset);

	/* convert english words to lower case */
	slice_eng_to_lower(slice);

	list li = LIST_NULL;
	li = text_segment(slice->mb_str);

	printf("slice<%u,%u>: %s\n",
	       slice->begin, slice->offset, slice->mb_str);

	list_foreach(&li, &get_term_pos, slice);
	list_release(&li);
}

static void lexer_file_input(const char *path)
{
	FILE *fh = fopen(path, "r");
	if (fh) {
		txtin = fh;
		txtlex();
		fclose(fh);
	} else {
		printf("cannot open `%s'.\n", path);
	}
}

static void index_txt_document(const char *fullpath)
{
	size_t val_sz = strlen(fullpath) + 1;
	doc_id_t new_docID;

	term_index_doc_begin(term_index);
	lexer_file_input(fullpath);
	new_docID = term_index_doc_end(term_index);

	if(keyval_db_put(keyval_db, &new_docID, sizeof(doc_id_t),
	                 (void *)fullpath, val_sz)) {
		printf("put error: %s\n", keyval_db_last_err(keyval_db));
		return;
	}
}

static int foreach_file_callbk(const char *filename, void *arg)
{
	char *path = (char*) arg;
	//char *ext = filename_ext(filename);
	char fullpath[MAX_FILE_NAME_LEN];

	//if (ext && strcmp(ext, ".txt") == 0) {
		sprintf(fullpath, "%s/%s", path, filename);
		//printf("[txt file] %s\n", fullpath);

		index_txt_document(fullpath);
		if (term_index_maintain(term_index))
			printf("\r[term index merging...]");
	//}

	return 0;
}

static enum ds_ret
dir_search_callbk(const char* path, const char *srchpath,
                  uint32_t level, void *arg)
{
	printf("[directory] %s\n", path);
	foreach_files_in(path, &foreach_file_callbk, (void*)path);
	return DS_RET_CONTINUE;
}

int main(int argc, char* argv[])
{
	int opt;
	char *keyval_db_path /* key value DB tmp string*/;
	char *path = NULL /* corpus path */;
	const char index_path[] = "./tmp";

	while ((opt = getopt(argc, argv, "hp:")) != -1) {
		switch (opt) {
		case 'h':
			printf("DESCRIPTION:\n");
			printf("index txt document from a specified path. \n");
			printf("\n");
			printf("USAGE:\n");
			printf("%s -h | -p <corpus path>\n", argv[0]);
			printf("\n");
			printf("EXAMPLE:\n");
			printf("%s -p ./some/where/file.txt\n", argv[0]);
			printf("%s -p ./some/where\n", argv[0]);
			goto exit;

		case 'p':
			path = strdup(optarg);
			break;

		default:
			printf("bad argument(s). \n");
			goto exit;
		}
	}

	if (path) {
		printf("corpus path %s\n", path);
	} else {
		printf("no corpus path specified.\n");
		goto exit;
	}

	printf("opening dict...\n");
	text_segment_init("../jieba/fork/dict");
	text_segment_insert_usrterm("当且仅当");
	printf("dict opened.\n");

	printf("opening term index...\n");
	term_index = term_index_open(index_path, TERM_INDEX_OPEN_CREATE);
	if (NULL == term_index) {
		printf("cannot create/open term index.\n");
		return 1;
	}

	keyval_db_path = (char *)malloc(1024);
	strcpy(keyval_db_path, index_path);
	strcat(keyval_db_path, "/kv_doc_path.bin");
	printf("opening key-value DB (%s)...\n", keyval_db_path);
	keyval_db = keyval_db_open(keyval_db_path, KEYVAL_DB_OPEN_WR);
	free(keyval_db_path);
	if (keyval_db == NULL) {
		printf("cannot create/open key-value DB.\n");
		goto keyval_db_fails;
	}

	if (file_exists(path)) {
		printf("[single file] %s\n", path);
		index_txt_document(path);
	} else if (dir_exists(path)) {
		dir_search_podfs(path, &dir_search_callbk, NULL);
	} else {
		printf("not file/directory.\n");
	}

	printf("closing key-value DB...\n");
	keyval_db_close(keyval_db);

keyval_db_fails:
	printf("closing term index...\n");
	term_index_close(term_index);

	printf("closing dict...\n");
	text_segment_free();

	free(path);
exit:
	return 0;
}
