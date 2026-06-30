#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EMPTY 0
#define BLACK 1
#define WHITE -1

const int WEIGHTS[64] = {
    100, -20,  10,   5,   5,  10, -20, 100,
    -20, -40,  -5,  -5,  -5,  -5, -40, -20,
     10,  -5,  15,   3,   3,  15,  -5,  10,
      5,  -5,   3,   3,   3,   3,  -5,   5,
      5,  -5,   3,   3,   3,   3,  -5,   5,
     10,  -5,  15,   3,   3,  15,  -5,  10,
    -20, -40,  -5,  -5,  -5,  -5, -40, -20,
    100, -20,  10,   5,   5,  10, -20, 100
};

const int DR[8] = {-1, -1, -1,  0, 0,  1, 1, 1};
const int DC[8] = {-1,  0,  1, -1, 1, -1, 0, 1};

clock_t start_time;
double time_limit = 3.0; // Hard cap at 3.0 seconds max
int search_cut_off = 0;

int check_direction(const int board[64], int r, int c, int dr, int dc, int player) {
    int opponent = -player;
    r += dr; c += dc;
    int count = 0;
    while (r >= 0 && r < 8 && c >= 0 && c < 8 && board[r * 8 + c] == opponent) {
        r += dr; c += dc;
        count++;
    }
    if (r >= 0 && r < 8 && c >= 0 && c < 8 && board[r * 8 + c] == player && count > 0) {
        return count;
    }
    return 0;
}

int evaluate_move(const int board[64], int index, int player) {
    if (board[index] != EMPTY) return 0;
    int r = index / 8, c = index % 8, total_flipped = 0;
    for (int d = 0; d < 8; d++) {
        total_flipped += check_direction(board, r, c, DR[d], DC[d], player);
    }
    return total_flipped;
}

void make_move(const int board[64], int next_board[64], int index, int player) {
    memcpy(next_board, board, 64 * sizeof(int));
    next_board[index] = player;
    int r = index / 8, c = index % 8;
    for (int d = 0; d < 8; d++) {
        if (check_direction(board, r, c, DR[d], DC[d], player) > 0) {
            int curr_r = r + DR[d], curr_c = c + DC[d];
            while (next_board[curr_r * 8 + curr_c] == -player) {
                next_board[curr_r * 8 + curr_c] = player;
                curr_r += DR[d]; curr_c += DC[d];
            }
        }
    }
}

int evaluate_board(const int board[64], int player) {
    int score = 0;
    for (int i = 0; i < 64; i++) {
        if (board[i] == player) score += WEIGHTS[i];
        else if (board[i] == -player) score -= WEIGHTS[i];
    }
    return score;
}

typedef struct { int index; int weight; } Move;
int compare_moves(const void* a, const void* b) {
    return ((Move*)b)->weight - ((Move*)a)->weight;
}

int alpha_beta(const int board[64], int depth, int alpha, int beta, int player, int is_max) {
    if (search_cut_off) return alpha; 
    if (depth == 0) return evaluate_board(board, is_max ? player : -player);

    // Check clock periodically at even depths to prevent time-checking lag
    if (depth % 2 == 0) {
        double elapsed = (double)(clock() - start_time) / CLOCKS_PER_SEC;
        if (elapsed >= time_limit) {
            search_cut_off = 1;
            return alpha;
        }
    }

    Move legal_moves[64];
    int move_count = 0;
    for (int i = 0; i < 64; i++) {
        if (evaluate_move(board, i, player) > 0) {
            legal_moves[move_count].index = i;
            legal_moves[move_count].weight = WEIGHTS[i];
            move_count++;
        }
    }

    if (move_count == 0) {
        int opponent_can_move = 0;
        for (int i = 0; i < 64; i++) {
            if (evaluate_move(board, i, -player) > 0) { opponent_can_move = 1; break; }
        }
        if (!opponent_can_move) return evaluate_board(board, is_max ? player : -player);
        return -alpha_beta(board, depth - 1, -beta, -alpha, -player, !is_max);
    }

    qsort(legal_moves, move_count, sizeof(Move), compare_moves);

    int best_val = -1000000;
    for (int i = 0; i < move_count; i++) {
        int next_board[64];
        make_move(board, next_board, legal_moves[i].index, player);
        int val = -alpha_beta(next_board, depth - 1, -beta, -alpha, -player, !is_max);
        
        if (search_cut_off) return best_val;

        if (val > best_val) best_val = val;
        if (val > alpha) alpha = val;
        if (alpha >= beta) break;
    }
    return best_val;
}

EMSCRIPTEN_KEEPALIVE
int get_best_move(const char* board_state, int max_depth, int active_player) {
    int board[64];
    for (int i = 0; i < 64; i++) {
        if (board_state[i] == 'X') board[i] = BLACK;
        else if (board_state[i] == 'O') board[i] = WHITE;
        else board[i] = EMPTY;
    }

    Move legal_moves[64];
    int move_count = 0;
    for (int i = 0; i < 64; i++) {
        if (evaluate_move(board, i, active_player) > 0) {
            legal_moves[move_count].index = i;
            legal_moves[move_count].weight = WEIGHTS[i];
            move_count++;
        }
    }

    if (move_count == 0) return -1;
    qsort(legal_moves, move_count, sizeof(Move), compare_moves);

    start_time = clock();
    search_cut_off = 0;
    int absolute_best_move = legal_moves[0].index;

    // ITERATIVE DEEPENING LOOP
    for (int d = 1; d <= max_depth; d++) {
        int best_move_this_depth = legal_moves[0].index;
        int alpha = -1000000, beta = 1000000;

        for (int i = 0; i < move_count; i++) {
            int next_board[64];
            make_move(board, next_board, legal_moves[i].index, active_player);
            int val = -alpha_beta(next_board, d - 1, -beta, -alpha, -active_player, 0);
            
            if (val > alpha) {
                alpha = val;
                best_move_this_depth = legal_moves[i].index;
            }
            if (search_cut_off) break;
        }

        if (!search_cut_off) {
            absolute_best_move = best_move_this_depth;
            
            // Move ordering trick: Put the best move found at this depth to the front 
            // of the list so the next depth level searches it first (causes instant cutoffs)
            for (int i = 0; i < move_count; i++) {
                if (legal_moves[i].index == absolute_best_move) {
                    Move temp = legal_moves[0];
                    legal_moves[0] = legal_moves[i];
                    legal_moves[i] = temp;
                    break;
                }
            }
        } else {
            break; // Time limit hit, exit loop
        }
    }

    return absolute_best_move;
}