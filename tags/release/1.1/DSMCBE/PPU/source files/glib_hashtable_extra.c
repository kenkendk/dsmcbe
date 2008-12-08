/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */
 
 /*
  * This is an extract of the glib functions that were not avalible in glib < 2.12.3 which we link against 
  */

#include <glib.h>
#include <glib/ghash.h>
#include <glib/gqueue.h>

typedef struct _GHashNode      GHashNode;

struct _GHashNode
{
  gpointer   key;
  gpointer   value;
  GHashNode *next;
};

struct _GHashTable
{
  gint             size;
  gint             nnodes;
  GHashNode      **nodes;
  GHashFunc        hash_func;
  GEqualFunc       key_equal_func;
  volatile gint    ref_count;
  GDestroyNotify   key_destroy_func;
  GDestroyNotify   value_destroy_func;
};

typedef struct
{
  GHashTable	*hash_table;
  GHashNode	*prev_node;
  GHashNode	*node;
  int		position;
  gboolean	pre_advanced;
} RealIter;


void
g_hash_table_iter_init (GHashTableIter *iter,
			GHashTable     *hash_table)
{
  RealIter *ri = (RealIter *) iter;

  g_return_if_fail (iter != NULL);
  g_return_if_fail (hash_table != NULL);

  ri->hash_table = hash_table;
  ri->prev_node = NULL;
  ri->node = NULL;
  ri->position = -1;
  ri->pre_advanced = FALSE;
}

gboolean
g_hash_table_iter_next (GHashTableIter *iter,
			gpointer       *key,
			gpointer       *value)
{
  RealIter *ri = (RealIter *) iter;

  g_return_val_if_fail (iter != NULL, FALSE);

  if (ri->pre_advanced)
    {
      ri->pre_advanced = FALSE;

      if (ri->node == NULL)
	return FALSE;
    }
  else
    {
      if (ri->node != NULL)
	{
	  ri->prev_node = ri->node;
	  ri->node = ri->node->next;
	}

      while (ri->node == NULL)
	{
	  ri->position++;
	  if (ri->position >= ri->hash_table->size)
	    return FALSE;

	  ri->prev_node = NULL;
	  ri->node = ri->hash_table->nodes[ri->position];
	}
    }

  if (key != NULL)
    *key = ri->node->key;
  if (value != NULL)
    *value = ri->node->value;

  return TRUE;
}

static void
iter_remove_or_steal (RealIter *ri, gboolean notify)
{
  GHashNode *prev;
  GHashNode *node;
  int position;

  g_return_if_fail (ri != NULL);
  g_return_if_fail (ri->node != NULL);

  prev = ri->prev_node;
  node = ri->node;
  position = ri->position;

  /* pre-advance the iterator since we will remove the node */

  ri->node = ri->node->next;
  /* ri->prev_node is still the correct previous node */

  while (ri->node == NULL)
    {
      ri->position++;
      if (ri->position >= ri->hash_table->size)
	break;

      ri->prev_node = NULL;
      ri->node = ri->hash_table->nodes[ri->position];
    }

  ri->pre_advanced = TRUE;

  /* remove the node */

  if (prev != NULL)
    prev->next = node->next;
  else
    ri->hash_table->nodes[position] = node->next;

  if (notify)
    {
      if (ri->hash_table->key_destroy_func)
	ri->hash_table->key_destroy_func(node->key);
      if (ri->hash_table->value_destroy_func)
	ri->hash_table->value_destroy_func(node->value);
    }

  g_slice_free (GHashNode, node);

  ri->hash_table->nnodes--;
}

void
g_hash_table_iter_remove (GHashTableIter *iter)
{
  iter_remove_or_steal ((RealIter *) iter, TRUE);
}

void
g_hash_table_iter_steal (GHashTableIter *iter)
{
  iter_remove_or_steal ((RealIter *) iter, FALSE);
}

GList *
g_hash_table_get_values (GHashTable *hash_table)
{
  GHashNode *node;
  gint i;
  GList *retval;

  g_return_val_if_fail (hash_table != NULL, NULL);

  retval = NULL;
  for (i = 0; i < hash_table->size; i++)
    for (node = hash_table->nodes[i]; node; node = node->next)
      retval = g_list_prepend (retval, node->value);

  return retval;
}

void
g_queue_clear (GQueue *queue)
{
  g_return_if_fail (queue != NULL);

  g_list_free (queue->head);
  g_queue_init (queue);
}

void
g_queue_init (GQueue *queue)
{
  g_return_if_fail (queue != NULL);

  queue->head = queue->tail = NULL;
  queue->length = 0;
}
