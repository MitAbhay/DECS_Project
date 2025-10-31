#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <stdint.h>

typedef struct sl_node
{
    int player_id;
    int score;
    struct sl_node **forward; // array of forward pointers
    int level;
} sl_node;

typedef struct skiplist
{
    struct sl_node *header;
    int level;
    int size;
} skiplist;

skiplist *sl_create();
void sl_free(skiplist *sl);
int sl_insert(skiplist *sl, int player_id, int score);
int sl_remove_by_player(skiplist *sl, int player_id);
int sl_get_score(skiplist *sl, int player_id, int *score_out);
int sl_get_top_n(skiplist *sl, int n, int *players_out, int *scores_out);
int sl_size(skiplist *sl);

#endif
