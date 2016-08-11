#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>

#include "txt-seg/config.h"
#include "txt-seg/txt-seg.h"
#include "wstring/wstring.h"

#include "config.h"
#include "search.h"
#include "search-utils.h"

void print_res_item(struct rank_hit* hit, uint32_t cnt, void* arg)
{
	char  *str;
	size_t str_sz;
	list   highlight_list;
	P_CAST(indices, struct indices, arg);

	printf("page result#%u: doc#%u score=%.3f\n", cnt, hit->docID, hit->score);

	/* get URL */
	str = get_blob_string(indices->url_bi, hit->docID, 0, &str_sz);
	printf("URL: %s" "\n", str);
	free(str);

	{
		int i;
		printf("occurs: ");
		for (i = 0; i < hit->n_occurs; i++)
			printf("%u ", hit->occurs[i]);
		printf("\n");
	}

	printf("\n");

	/* get document text */
	str = get_blob_string(indices->txt_bi, hit->docID, 1, &str_sz);

	/* prepare highlighter arguments */
	highlight_list = prepare_snippet(hit, str, str_sz,
	                                 lex_mix_file);//lex_eng_file);
	free(str);

	/* print snippet */
	snippet_hi_print(&highlight_list);
	printf("--------\n\n");

	/* free highlight list */
	snippet_free_highlight_list(&highlight_list);
}

void
print_res(ranked_results_t *rk_res, uint32_t page, struct indices *indices)
{
	struct rank_window win;
	uint32_t tot_pages;

	win = rank_window_calc(rk_res, page, DEFAULT_RES_PER_PAGE, &tot_pages);
	if (page >= tot_pages)
		printf("No such page. (total page(s): %u)\n", tot_pages);

	if (win.to > 0) {
		printf("page %u/%u, top result(s) from %u to %u:\n",
			   page + 1, tot_pages, win.from + 1, win.to);
		rank_window_foreach(&win, &print_res_item, indices);
	}
}

int main(int argc, char *argv[])
{
	struct query_keyword keyword;
	struct query         qry;
	struct indices       indices;
	int                  opt;
	uint32_t             page = 1;
	char                *index_path = NULL;
	ranked_results_t     results;

	/* initialize text segmentation module */
	printf("opening dictionary...\n");
	text_segment_init("../jieba/fork/dict");

	/* a single new query */
	qry = query_new();

	while ((opt = getopt(argc, argv, "hi:p:t:m:x:")) != -1) {
		switch (opt) {
		case 'h':
			printf("DESCRIPTION:\n");
			printf("testcase of search function.\n");
			printf("\n");
			printf("USAGE:\n");
			printf("%s -h |"
			       " -i <index path> |"
			       " -p <page> |"
			       " -t <term> |"
			       " -m <tex> |"
			       " -x <text>"
			       "\n", argv[0]);
			printf("\n");
			goto exit;

		case 'i':
			index_path = strdup(optarg);
			break;

		case 'p':
			sscanf(optarg, "%u", &page);
			break;

		case 't':
			keyword.type = QUERY_KEYWORD_TERM;
			wstr_copy(keyword.wstr, mbstr2wstr(optarg));
			query_push_keyword(&qry, &keyword);
			break;

		case 'm':
			keyword.type = QUERY_KEYWORD_TEX;
			keyword.pos = 0; /* do not care for now */
			wstr_copy(keyword.wstr, mbstr2wstr(optarg));
			query_push_keyword(&qry, &keyword);
			break;

		case 'x':
			query_digest_utf8txt(&qry, optarg);
			break;

		default:
			printf("bad argument(s). \n");
			goto exit;
		}
	}

	/*
	 * check program arguments.
	 */
	if (index_path == NULL || qry.len == 0) {
		printf("not enough arguments.\n");
		goto exit;
	}

	/*
	 * open indices
	 */
	printf("opening index at path: `%s' ...\n", index_path);
	if (indices_open(&indices, index_path, INDICES_OPEN_RD)) {
		printf("index open failed.\n");
		goto close;
	}

	/* setup cache */
	indices_cache(&indices, 32 MB);

	/*
	 * pause and continue on key press to have an idea
	 * of how long the actual search process takes.
	 */
	printf("Press Enter to Continue");
	while(getchar() != '\n');

	/* search query */
	results = indices_run_query(&indices, qry);

	/* print ranked search results in pages */
	print_res(&results, page - 1, &indices);

	/* free ranked results */
	free_ranked_results(&results);

close:
	/*
	 * close indices
	 */
	printf("closing index...\n");
	indices_close(&indices);

exit:
	printf("existing...\n");

	/*
	 * free program arguments
	 */
	if (index_path)
		free(index_path);

	/*
	 * free other program modules
	 */
	query_delete(qry);
	text_segment_free();
	return 0;
}