/*  DreamChess
**
**  DreamChess is the legal property of its developers, whose names are too
**  numerous to list here. Please refer to the COPYRIGHT file distributed
**  with this source distribution.
**
**  This program is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dreamer.h"
#include "e_comm.h"
#include "move.h"
#include "history.h"
#include "repetition.h"
#include "transposition.h"
#include "config.h"
#include "svn_version.h"
#include "search.h"
#include "san.h"

static int is_coord_move(char *ms)
{
    int len = strlen(ms);
    /* Check format. */
    if ((len < 4) || (len > 5))
        return 0;
    if ((ms[0] < 'a') || (ms[0] > 'h') ||
            (ms[1] < '1') || (ms[1] > '8') ||
            (ms[2] < 'a') || (ms[2] > 'h') ||
            (ms[3] < '1') || (ms[3] > '8'))
        return 0;

    if (len == 5)
        switch(ms[4])
        {
        case 'q':
        case 'r':
        case 'n':
        case 'b':
            return 1;
        default:
            return 0;
        }

    return 1;
}

static int convert_piece(int san_piece)
{
    switch (san_piece)
    {
    case SAN_KING:
        return KING;
    case SAN_QUEEN:
        return QUEEN;
    case SAN_ROOK:
        return ROOK;
    case SAN_KNIGHT:
        return KNIGHT;
    case SAN_BISHOP:
        return BISHOP;
    case SAN_PAWN:
        return PAWN;
    }
}

static move_t get_san_move(board_t *board, san_move_t *san)
{
    int source, dest;
    move_t move;
    int piece;
    bitboard_t en_passant = board->en_passant;
    int castle_flags = board->castle_flags;
    int fifty_moves = board->fifty_moves;
    int found = 0;
    move_t found_move;

    if (san->type == SAN_QUEENSIDE_CASTLE)
    {
        san->source_file = 4;
        san->source_rank = (board->current_player == SIDE_WHITE ? 0 : 7);
        san->destination = (board->current_player == SIDE_WHITE ? 2 : 58);
        piece = KING + board->current_player;
    }
    else if (san->type == SAN_KINGSIDE_CASTLE)
    {
        san->source_file = 4;
        san->source_rank = (board->current_player == SIDE_WHITE ? 0 : 7);
        san->destination = (board->current_player == SIDE_WHITE ? 6 : 62);
        piece = KING + board->current_player;
    }
    else
        piece = convert_piece(san->piece) + board->current_player;

    compute_legal_moves(board, 0);

    /* Look for move in list. */
    while ((move = move_next(board, 0)) != NO_MOVE)
    {
        int move_piece;

        if (MOVE_GET(move, DEST) != san->destination)
            continue;

        if (board->current_player == SIDE_WHITE)
            move_piece = find_white_piece(board, MOVE_GET(move, SOURCE));
        else
            move_piece = find_black_piece(board, MOVE_GET(move, SOURCE));

        if (move_piece != piece)
            continue;

        if (san->source_file != SAN_NOT_SPECIFIED)
            if (san->source_file != MOVE_GET(move, SOURCE) % 8)
                continue;

        if (san->source_rank != SAN_NOT_SPECIFIED)
            if (san->source_rank != MOVE_GET(move, SOURCE) / 8)
                continue;

        if (san->type == SAN_CAPTURE)
        {
            /* TODO verify en passant capture? */
            if (!(move & (CAPTURE_MOVE_EN_PASSANT | CAPTURE_MOVE)))
                    continue;
        }

        if ((move & PROMOTION_MOVE_QUEEN) && (san->promotion_piece != SAN_QUEEN))
            continue;
        if ((move & PROMOTION_MOVE_ROOK) && (san->promotion_piece != SAN_ROOK))
            continue;
        if ((move & PROMOTION_MOVE_BISHOP) && (san->promotion_piece != SAN_BISHOP))
            continue;
        if ((move & PROMOTION_MOVE_KNIGHT) && (san->promotion_piece != SAN_KNIGHT))
            continue;
        if (!(move & MOVE_PROMOTION_MASK) && (san->promotion_piece != SAN_NOT_SPECIFIED))
            continue;

        /* TODO verify check and checkmate flags? */

        execute_move(board, move);
        board->current_player = OPPONENT(board->current_player);
        if (!is_check(board, 0))
        {
            found++;
            found_move = move;
        }
 
        board->current_player = OPPONENT(board->current_player);
        unmake_move(board, move, en_passant, castle_flags, fifty_moves);
    }

    if (found != 1)
        return NO_MOVE;

    return found_move;
}

static move_t get_coord_move(board_t *board, char *ms)
{
    int source, dest;
    move_t move;

    source = (ms[0] - 'a') + 8 * (ms[1] - '1');
    dest = (ms[2] - 'a') + 8 * (ms[3] - '1');

    compute_legal_moves(board, 0);

    /* Look for move in list. */
    while ((move = move_next(board, 0)) != NO_MOVE)
    {
        if ((MOVE_GET(move, SOURCE) == source) && (MOVE_GET(move, DEST) == dest))
        {
            bitboard_t en_passant = board->en_passant;
            int castle_flags = board->castle_flags;
            int fifty_moves = board->fifty_moves;

            /* Move found. */
            execute_move(board, move);
            board->current_player = OPPONENT(board->current_player);
            if (!is_check(board, 0))
            {
                board->current_player = OPPONENT(board->current_player);
                unmake_move(board, move, en_passant, castle_flags, fifty_moves);
                break;
            }
            board->current_player = OPPONENT(board->current_player);
            unmake_move(board, move, en_passant, castle_flags, fifty_moves);
        }
    }
    if (move != NO_MOVE)
    {
        if (move & MOVE_PROMOTION_MASK)
        {
            /* Set correct promotion piece. */
            move &= ~MOVE_PROMOTION_MASK;
            if (strlen(ms) == 5)
                switch(ms[4])
                {
                case 'q':
                    move |= PROMOTION_MOVE_QUEEN;
                    break;
                case 'r':
                    move |= PROMOTION_MOVE_ROOK;
                    break;
                case 'n':
                    move |= PROMOTION_MOVE_KNIGHT;
                    break;
                case 'b':
                    move |= PROMOTION_MOVE_BISHOP;
                }
            else
            {
                /* No promotion piece specified. */
                return NO_MOVE;
            }
            return move;
        }
        if (strlen(ms) == 4)
            return move;
    }
    return NO_MOVE;
}

char *coord_move_str(move_t move)
{
    char *ret = malloc(6);
    ret[5] = '\0';
    ret[0] = 'a' + MOVE_GET(move, SOURCE) % 8;
    ret[1] = '1' + MOVE_GET(move, SOURCE) / 8;
    ret[2] = 'a' + MOVE_GET(move, DEST) % 8;
    ret[3] = '1' + MOVE_GET(move, DEST) / 8;
    switch (move & MOVE_PROMOTION_MASK)
    {
    case PROMOTION_MOVE_QUEEN:
        ret[4] = 'q';
        break;
    case PROMOTION_MOVE_BISHOP:
        ret[4] = 'b';
        break;
    case PROMOTION_MOVE_KNIGHT:
        ret[4] = 'n';
        break;
    case PROMOTION_MOVE_ROOK:
        ret[4] = 'r';
        break;
    default:
        ret[4] = '\0';
    }
    return ret;
}

static void error(char *type, char *command)
{
    e_comm_send("Error (%s): %s\n", type, command);
}

#define NOT_NOW(c) error("command not legal now", c)
#define UNKNOWN(c) error("unknown command", c)
#define BADPARAM(c) error("invalid or missing parameter(s)", c)

static int parse_time_control(state_t *state, char *s)
{
    struct time_control t;
    char *end;
    char *semi;

    errno = 0;
    t.mps = strtol(s, &end, 10);

    if (errno || *end != ' ')
        return 1;

    semi = strchr(end + 1, ':');

    if (semi) {
        *semi = ' ';
        t.base = strtol(end + 1, &end, 10) * 60 * 100;
        *semi = ':';

        if (errno || end != semi)
            return 1;

        t.base += strtol(end + 1, &end, 10) * 100;

        if (errno || *end != ' ')
            return 1;
    } else {
        t.base = strtol(end + 1, &end, 10) * 60 * 100;

        if (errno || *end != ' ')
            return 1;
    }

    t.inc = strtol(end + 1, &end, 10) * 100;

    if (errno || *end != 0)
        return 1;

    /* Time control with both mps and inc is invalid */
    if (t.mps != 0 && t.inc != 0)
        return 1;

    state->time = t;
    return 0;
}

static int command_always(state_t *state, char *command)
{
    if (!strcmp(command, "post"))
    {
        set_option(OPTION_POST, 1);
        return 1;
    }

    if (!strcmp(command, "nopost"))
    {
        set_option(OPTION_POST, 0);
        return 1;
    }

    if (!strcmp(command, "hint"))
    {
        move_t move = state->ponder_opp_move;

        if (move == NO_MOVE)
            move = state->hint;

        if (move != NO_MOVE)
        {
            char *str = coord_move_str(state->hint);
            e_comm_send("Hint: %s\n", str);
            free(str);
        }
        return 1;
    }

    if (!strncmp(command, "time ", 5))
    {
        int time;
        char *end;
        errno = 0;
        time = strtol(command + 5, &end, 10);
        if (errno || *end != 0)
            BADPARAM(command);
        else
            state->engine_time = time;
        return 1;
    }

    if (!strncmp(command, "otim ", 5))
    {
        int time;
        char *end;
        errno = 0;
        time = strtol(command + 5, &end, 10);
        if (errno || *end != 0)
            BADPARAM(command);
        else
            state->opponent_time = time;
        return 1;
    }

    return 0;
}

int parse_move(board_t *board, char *command, move_t *move)
{
    san_move_t *san;

    san = san_parse(command);

    if (san)
    {
        *move = get_san_move(board, san);
        free(san);
        return 0;
    }
    else if (is_coord_move(command))
    {
        *move = get_coord_move(board, command);
        return 0;
    }

    return 1;
}

int command_usermove(state_t *state, char *command)
{
    move_t move;

    if (!parse_move(&state->board, command, &move))
    {
        if (move == NO_MOVE) {
            e_comm_send("Illegal move: %s\n", command);
            return 0;
        }

        if (my_turn(state))
        {
            NOT_NOW(command);
            return 0;
        }

        do_move(state, move);
        check_game_end(state);

        if (state->mode == MODE_IDLE)
            state->mode = (state->board.current_player == SIDE_WHITE?
                           MODE_WHITE : MODE_BLACK);

        if (state->ponder_my_move != NO_MOVE) {
            /* We already have a possible answer to this move from pondering. */
            if (move == state->ponder_opp_move)
            {
                /* User made the expected move. */
                send_move(state, state->ponder_my_move);
            }

            state->ponder_my_move = NO_MOVE;
        }

        return 0;
    }

    return 1;
}

void command_handle(state_t *state, char *command)
{
    san_move_t *san;
    move_t move = NO_MOVE;

    if (command_always(state, command))
        return;

    if (!strcmp(command, "xboard"))
    {
        /* xboard mode is default. */
        return;
    }

    if (!strncmp(command, "protover ", 9))
    {
        char *endptr;
        errno = 0;
        strtol(command + 9, &endptr, 10);

        if (errno || (*endptr != 0))
             BADPARAM(command);

        e_comm_send("feature setboard=1\n");
        e_comm_send("feature san=1\n");
        e_comm_send("feature myname=\"Dreamer v" PACKAGE_VERSION " (r" SVN_VERSION ")\"\n");
        e_comm_send("feature done=1\n");
        return;
    }

    if (!strncmp(command, "level ", 6))
    {
        if (parse_time_control(state, command + 6))
            BADPARAM(command);

        return;
    }

    if (!strncmp(command, "accepted ", 9))
    {
        if (!strcmp(command + 9, "setboard") || !strcmp(command + 9, "done"))
            return;

        BADPARAM(command);
        return;
    }

    if (!strcmp(command, "new"))
    {
        setup_board(&state->board);
        forget_history();
        clear_table();
        pv_clear();
        repetition_init(&state->board);
        state->flags = 0;
        state->depth = MAX_DEPTH;
        state->mode = MODE_BLACK;
        state->done = 0;
        state->hint = NO_MOVE;
        state->ponder_opp_move = NO_MOVE;
        state->ponder_my_move = NO_MOVE;
        return;
    }

    if (!strcmp(command, "quit"))
    {
        state->mode = MODE_QUIT;
        return;
    }

    if (!strcmp(command, "force"))
    {
        state->mode = MODE_FORCE;
        return;
    }

    if (!strcmp(command, "white"))
    {
        if (state->board.current_player != SIDE_WHITE)
        {
            /* FIXME, we should support this, but right now it would cause
            ** severe problems with the undo system.
            */
            NOT_NOW(command);
            return;
        }
        state->mode = MODE_BLACK;
        return;
    }

    if (!strcmp(command, "black"))
    {
        if (state->board.current_player != SIDE_BLACK)
        {
            /* FIXME, see above. */
            NOT_NOW(command);
            return;
        }
        state->mode = MODE_WHITE;
        return;
    }

    if (!strcmp(command, "playother"))
    {
        if (state->mode != MODE_FORCE)
        {
            NOT_NOW(command);
            return;
        }

        if (state->board.current_player == SIDE_WHITE)
            state->mode = MODE_BLACK;
        else
            state->mode = MODE_WHITE;
        return;
    }

    if (!strncmp(command, "sd", 2))
    {
        char *number = strchr(command, ' ');
        char *end;
        if (number && (*(number + 1) != '\0'))
        {
            long int val = strtol(number + 1, &end, 10);
            if ((*end == '\0') && (val > 0))
                state->depth = val;
            else
                BADPARAM(command);
        }
        else
            BADPARAM(command);
        return;
    }

    if (!strcmp(command, "go"))
    {
        if (state->board.current_player == SIDE_WHITE)
            state->mode = MODE_WHITE;
        else
            state->mode = MODE_BLACK;
        return;
    }

    if (!strcmp(command, "remove"))
    {
        switch (state->mode)
        {
        case MODE_WHITE:
            if (state->board.current_player == SIDE_WHITE)
            {
                NOT_NOW(command);
                return;
            }
            break;
        case MODE_BLACK:
            if (state->board.current_player == SIDE_BLACK)
            {
                NOT_NOW(command);
                return;
            }
            break;
        case MODE_FORCE:
            break;
        default:
            NOT_NOW(command);
            return;
        }

        if (state->moves < 2)
            NOT_NOW(command);

        undo_move(state);
        undo_move(state);
        state->done = 0;
        return;
    }

    if (!strcmp(command, "?"))
    {
        NOT_NOW(command);
        return;
    }

    if (!strncmp(command, "setboard ", 9))
    {
        board_t board;

        if (state->mode != MODE_FORCE)
        {
            NOT_NOW(command);
            return;
        }

        if (setup_board_fen(&board, command + 9))
        {
            BADPARAM(command);
            return;
        }

        state->board = board;
        forget_history();
        clear_table();
        repetition_init(&state->board);
        state->done = 0;
        return;
    }

    if (!strcmp(command, "noquiesce"))
    {
        set_option(OPTION_QUIESCE, 0);
        return;
    }

    if (!strcmp(command, "easy"))
    {
        set_option(OPTION_PONDER, 0);
        state->ponder_my_move = NO_MOVE;
        return;
    }

    if (!strcmp(command, "hard"))
    {
        set_option(OPTION_PONDER, 1);
        if (state->moves != 0)
            state->flags |= FLAG_PONDER;
        return;
    }

    if (!command_usermove(state, command))
        return;

    UNKNOWN(command);
}

int command_check_abort(state_t *state, char *command)
{
    if (!strcmp(command, "?"))
    {
        if (my_turn(state))
            return 1;
    }

    if (command_always(state, command))
        return 0;

    if (!strcmp(command, "new"))
    {
        state->flags = FLAG_NEW_GAME | FLAG_IGNORE_MOVE;
        return 1;
    }

    if (!strcmp(command, "quit") || !strcmp(command, "force"))
    {
        state->flags = FLAG_IGNORE_MOVE;
        command_handle(state, command);
        return 1;
    }

    if (!strcmp(command, "easy"))
    {
        if (state->flags & FLAG_PONDER)
        {
            state->flags = FLAG_IGNORE_MOVE;
            state->ponder_actual_move = NO_MOVE;
        }
        command_handle(state, command);
        return 1;
    }

    if (state->flags & FLAG_PONDER)
    {
        move_t move;

        if (!parse_move(&state->root_board, command, &move))
        {
            if (move == NO_MOVE) {
                e_comm_send("Illegal move: %s\n", command);
                return 0;
            }

            if (move == state->ponder_opp_move)
            {
                /* User made the expected move. */
                state->flags = 0;
                return 0;
            }
            else
            {
                /* User made a different move, abort and restart search. */
                state->flags = FLAG_IGNORE_MOVE;
                state->ponder_actual_move = move;
                return 1;
            }
        }
    }

    NOT_NOW(command);
    return 0;
}
