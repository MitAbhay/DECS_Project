#include "skiplist.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int sl_random_level()
{
    int lvl = 1;
    while ((rand() & 0xFFFF) < (0.5 * 0xFFFF) && lvl < SKIPLIST_MAX_LEVEL)
        lvl++;
    return lvl;
}

static sl_node *sl_create_node(int level, int player_id, int score)
{
    sl_node *n = malloc(sizeof(sl_node));
    n->player_id = player_id;
    n->score = score;
    n->level = level;
    n->forward = malloc(sizeof(sl_node *) * (level + 1));
    for (int i = 0; i <= level; i++)
        n->forward[i] = NULL;
    return n;
}

skiplist *sl_create()
{
    srand(time(NULL));
    skiplist *sl = malloc(sizeof(skiplist));
    sl->level = 1;
    sl->size = 0;
    sl->header = sl_create_node(SKIPLIST_MAX_LEVEL, -1, -1);
    return sl;
}

void sl_free(skiplist *sl)
{
    sl_node *node = sl->header->forward[0];
    while (node)
    {
        sl_node *next = node->forward[0];
        free(node->forward);
        free(node);
        node = next;
    }
    free(sl->header->forward);
    free(sl->header);
    free(sl);
}

int sl_insert(skiplist *sl, int player_id, int score)
{
    sl_node *update[SKIPLIST_MAX_LEVEL + 1];
    sl_node *x = sl->header;
    for (int i = sl->level; i >= 0; i--)
    {
        while (x->forward[i] && (x->forward[i]->score > score || (x->forward[i]->score == score && x->forward[i]->player_id < player_id)))
        {
            x = x->forward[i];
        }
        update[i] = x;
    }
    x = x->forward[0];
    // if player exists, remove old node and re-insert with new score
    if (x && x->player_id == player_id)
    {
        // remove existing node (simpler approach: remove and then insert)
        sl_remove_by_player(sl, player_id);
    }
    int lvl = sl_random_level();
    if (lvl > sl->level)
    {
        for (int i = sl->level + 1; i <= lvl; i++)
            update[i] = sl->header;
        sl->level = lvl;
    }
    sl_node *newnode = sl_create_node(lvl, player_id, score);
    for (int i = 0; i <= lvl; i++)
    {
        newnode->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = newnode;
    }
    sl->size++;
    return 0;
}

int sl_remove_by_player(skiplist *sl, int player_id)
{
    sl_node *update[SKIPLIST_MAX_LEVEL + 1];
    sl_node *x = sl->header;
    for (int i = sl->level; i >= 0; i--)
    {
        while (x->forward[i] && x->forward[i]->player_id < player_id && x->forward[i]->score >= 0)
        {
            // Not perfect ordering by player id, but we need a search by scanning level 0
            break;
        }
        update[i] = x;
    }
    // linear search at level 0
    sl_node *cur = sl->header->forward[0];
    sl_node *prev = sl->header;
    while (cur)
    {
        if (cur->player_id == player_id)
        {
            // remove cur
            for (int i = 0; i <= sl->level; i++)
            {
                if (update[i]->forward[i] == cur)
                    update[i]->forward[i] = cur->forward[i];
            }
            free(cur->forward);
            free(cur);
            sl->size--;
            while (sl->level > 0 && sl->header->forward[sl->level] == NULL)
                sl->level--;
            return 0;
        }
        prev = cur;
        cur = cur->forward[0];
    }
    return -1;
}

int sl_get_score(skiplist *sl, int player_id, int *score_out)
{
    sl_node *cur = sl->header->forward[0];
    while (cur)
    {
        if (cur->player_id == player_id)
        {
            *score_out = cur->score;
            return 0;
        }
        cur = cur->forward[0];
    }
    return -1;
}

int sl_get_top_n(skiplist *sl, int n, int *players_out, int *scores_out)
{
    sl_node *cur = sl->header->forward[0];
    int idx = 0;
    while (cur && idx < n)
    {
        players_out[idx] = cur->player_id;
        scores_out[idx] = cur->score;
        idx++;
        cur = cur->forward[0];
    }
    return idx;
}

int sl_size(skiplist *sl)
{
    return sl->size;
}
