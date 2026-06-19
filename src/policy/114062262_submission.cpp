#include <utility>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstdlib>

#include "state.hpp"
#include "114062262_submission.hpp"

/*============================================================
 * Move ordering helper
 * PVS 很吃 move ordering，所以 capture 要排前面
 *============================================================*/
enum BoundType {
    TT_EXACT,
    TT_LOWER,
    TT_UPPER
};

struct TTNode {
    int depth;
    int score;
    BoundType bound;
    Move best_move;
};

static std::unordered_map<uint64_t, TTNode> trans_table;
static uint64_t last_root_key = 0;

static const int official_material[7] = {
    0,
    200,
    600,
    700,
    800,
    2000,
    10000
};

static uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static uint64_t tt_key(State* state) {
    return state->hash() ^ mix64((uint64_t)state->step + 1ULL);
}

static int material_balance(State* state, int player) {
    int self = 0;
    int oppn = 0;
    int other = 1 - player;

    for (int r = 0; r < BOARD_H; r++) {
        for (int c = 0; c < BOARD_W; c++) {
            self += official_material[state->piece_at(player, r, c)];
            oppn += official_material[state->piece_at(other, r, c)];
        }
    }

    return self - oppn;
}

static bool deadline_score(State* state, int ply, int& score) {
    if (state->step < MAX_STEP) {
        return false;
    }

    int balance = material_balance(state, state->player);

    if (balance > 0) {
        score = P_MAX - 500 - ply;
    } else if (balance < 0) {
        score = M_MAX + 500 + ply;
    } else {
        score = 0;
    }

    return true;
}
static bool repetition_draw_score(
    State* state,
    const GameHistory& history,
    int& score
) {
    return state->check_repetition(history, score);
}

static int repeated_position_bias(State* state, const GameHistory& history) {
    if (history.count(state->hash()) == 0) {
        return 0;
    }

    int balance = material_balance(state, state->player);

    if (balance > 0) {
        return -1000 - std::min(balance, 8000);
    }

    if (balance < 0) {
        return 1000 + std::min(-balance, 8000);
    }

    return -200;
}

static int root_repetition_penalty(State* state, const GameHistory& history) {
    int repeats = history.count(state->hash());

    if (repeats >= 3) {
        return 180000;
    }

    if (repeats == 2) {
        return 90000;
    }

    if (repeats == 1) {
        return 30000;
    }

    return 0;
}

static int orient_child_score(int child_score, bool same) {
    return same ? child_score : -child_score;
}

static int move_score(State* state, const Move& m) {
    int from_r = m.first.first;
    int from_c = m.first.second;
    int to_r   = m.second.first;
    int to_c   = m.second.second;

    int moving = state->piece_at(state->player, from_r, from_c);
    int captured = state->piece_at(1 - state->player, to_r, to_c);

    static const int val[7] = {
        0,      // empty
        200,    // pawn
        600,    // rook
        700,    // knight
        800,    // bishop
        2000,   // queen
        100000  // king
    };

    int score = 0;

    // 1. capture 優先
    if (captured) {
        score += 100000 + val[captured] * 10;

        if (moving) {
            score -= val[moving];
        }

        // 如果吃到的是快升變的 pawn，要更優先
        if (captured == 1) {
            int danger = 0;

            if (state->player == 0) {
                // white 吃 black pawn：black pawn row 越大越危險
                danger = to_r;
            } else {
                // black 吃 white pawn：white pawn row 越小越危險
                danger = BOARD_H - 1 - to_r;
            }

            score += danger * danger * 200;
        }
    }
    
    

    // 2. pawn 前進 bonus
    if (moving == 1) {
        if (state->player == 0) {
            score += (from_r - to_r) * 50;  // white row 變小
        } else {
            score += (to_r - from_r) * 50;  // black row 變大
        }
    }
    // 3. 中央 bonus
    score += 5 - abs(to_c - 2);

    return score;
}

static std::vector<Move> get_ordered_moves(
    State* state,
    const Move* tt_move = nullptr
) {
    std::vector<Move> actions = state->legal_actions;

    std::sort(actions.begin(), actions.end(),
        [state, tt_move](const Move& a, const Move& b) {
            if (tt_move != nullptr && a == *tt_move) {
                return true;
            }

            if (tt_move != nullptr && b == *tt_move) {
                return false;
            }

            return move_score(state, a) > move_score(state, b);
        }
    );

    return actions;
}

static bool legal_move_exists(State* state, const Move& wanted) {
    for (const Move& action : state->legal_actions) {
        if (action == wanted) {
            return true;
        }
    }

    return false;
}

static bool opening_book_move(State* state, Move& book_move) {
    if (state->step == 0 && state->player == 0) {
        Move queenside_space(Point(4, 1), Point(3, 1)); // b2b3
        if (legal_move_exists(state, queenside_space)) {
            book_move = queenside_space;
            return true;
        }
    }

    if (
        state->step == 1
        && state->player == 1
        && state->piece_at(0, 3, 1) == 1
        && state->piece_at(0, 4, 1) == 0
    ) {
        Move central_space(Point(1, 3), Point(2, 3)); // d5d4
        if (legal_move_exists(state, central_space)) {
            book_move = central_space;
            return true;
        }
    }

    return false;
}


/*============================================================
 * Helper: search child from current player's perspective
 *
 * 如果 child 還是同一個 player：不用反號
 * 如果 child 換成對手：用 negamax 反號
 *============================================================*/
static int search_child_pvs(
    State* next,
    bool same,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const SubmissionParams& p
) 
{
    if (same) {
        return submission::eval_ctx(
            next,
            depth,
            alpha,
            beta,
            history,
            ply,
            ctx,
            p
        );
    }

    return -submission::eval_ctx(
        next,
        depth,
        -beta,
        -alpha,
        history,
        ply,
        ctx,
        p
    );
}

int submission::quiescence(
    State* state,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const SubmissionParams& p
) {
    ctx.nodes++;

    if (ply > ctx.seldepth) {
        ctx.seldepth = ply;
    }

    if (ctx.stop) {
        return 0;
    }

    if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
        state->get_legal_actions();
    }

    if (state->game_state == WIN) {
        return P_MAX - ply;
    }

    if (state->game_state == DRAW) {
        return 0;
    }

    int deadline = 0;
    if (deadline_score(state, ply, deadline)) {
        return deadline;
    }


    // 先用目前局面 evaluate
    int stand_pat = state->evaluate(
        p.use_kp_eval,
        p.use_eval_mobility,
        &history
    );

    if (stand_pat >= beta) {
        return beta;
    }

    if (stand_pat > alpha) {
        alpha = stand_pat;
    }

    std::vector<Move> actions = get_ordered_moves(state);

    // Quiescence 只搜尋 capture move
    for (const Move& action : actions) {
        int to_r = action.second.first;
        int to_c = action.second.second;

        int captured = state->piece_at(1 - state->player, to_r, to_c);

        if (!captured) {
            continue;//quiescence search 只延伸 capture moves
        }

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int score;
        int rep_score = 0;

        if (repetition_draw_score(next, history, rep_score)) {
            score = orient_child_score(rep_score, same);
        } else {
            history.push(next->hash());

            if (same) {
                score = quiescence(
                    next,
                    alpha,
                    beta,
                    history,
                    ply + 1,
                    ctx,
                    p
                );
            } else {
                score = -quiescence(
                    next,
                    -beta,
                    -alpha,
                    history,
                    ply + 1,
                    ctx,
                    p
                );
            }

            history.pop(next->hash());
        }

        delete next;

        if (score >= beta) {
            return beta;
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

/*============================================================
 * PVS — eval_ctx
 *
 * Principal Variation Search
 *
 * 第一個 child：full window
 * 後面 child：先 null window
 * 如果 null window 可能超過 alpha，再 full window re-search
 *============================================================*/
int submission::eval_ctx(
    State* state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const SubmissionParams& p
) {
    ctx.nodes++;
    if (ply > ctx.seldepth) {
        ctx.seldepth = ply;
    }

    if (ctx.stop) {
        return 0;
    }
    // if (ply >= 14) {
    //     return state->evaluate(
    //         p.use_kp_eval,
    //         p.use_eval_mobility,
    //         &history
    //     );
    // }
    // Lazy move generation
    if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
        state->get_legal_actions();
    }

    // Terminal
    if (state->game_state == WIN) {
        return P_MAX - ply;
    }

    if (state->game_state == DRAW) {
        return 0;
    }

    int terminal_score = 0;
    if (deadline_score(state, ply, terminal_score)) {
        return terminal_score;
    }

    // Repetition
    int rep_score = 0;
    if (repetition_draw_score(state, history, rep_score)) {
        return rep_score;
    }
    int alpha_start = alpha;
    int beta_start = beta;

    uint64_t key = tt_key(state);

    Move tt_move;
    bool has_tt_move = false;

    auto found = trans_table.find(key);

    if (found != trans_table.end()) {
        TTNode& node = found->second;

        if (node.depth >= depth) {
            if (node.bound == TT_EXACT) {
                return node.score;
            }

            if (node.bound == TT_LOWER) {
                alpha = std::max(alpha, node.score);
            } else if (node.bound == TT_UPPER) {
                beta = std::min(beta, node.score);
            }

            if (alpha >= beta) {
                return node.score;
            }
        }

        tt_move = node.best_move;
        has_tt_move = true;
    }

    history.push(state->hash());

    // Leaf
    if (depth <= 0) {
        int score = quiescence(
            state,
            alpha,
            beta,
            history,
            ply,
            ctx,
            p
        );

        history.pop(state->hash());
        return score;
    }

    std::vector<Move> actions = get_ordered_moves(
        state,
        has_tt_move ? &tt_move : nullptr
    );

    int best_score = M_MAX;
    bool first_child = true;
    Move best_move;
    bool has_best_move = false;

for (const Move& action : actions) {
    State* next = state->next_state(action);
    bool same = next->same_player_as_parent();

    int score;

    if (first_child) {
        // 第一個 child 用 full window
        score = search_child_pvs(
            next,
            same,
            depth - 1,
            alpha,
            beta,
            history,
            ply + 1,
            ctx,
            p
        );

        first_child = false;
    } else {
        // 其他 child 先用 null window 搜
        score = search_child_pvs(
            next,
            same,
            depth - 1,
            alpha,
            alpha + 1,
            history,
            ply + 1,
            ctx,
            p
        );

        // 如果它真的可能比 alpha 好，再 full window re-search
        if (score > alpha && score < beta) {
            score = search_child_pvs(
                next,
                same,
                depth - 1,
                alpha,
                beta,
                history,
                ply + 1,
                ctx,
                p
            );
        }
    }

    delete next;
    
    if (score > best_score) {
        best_score = score;
        best_move = action;
        has_best_move = true;
    }

    if (score > alpha) {
        alpha = score;
    }

    if (alpha >= beta) {
        break;
    }
}
if (has_best_move) {
    TTNode save;
    save.depth = depth;
    save.score = best_score;
    save.best_move = best_move;

    if (best_score <= alpha_start) {
        save.bound = TT_UPPER;
    } else if (best_score >= beta_start) {
        save.bound = TT_LOWER;
    } else {
        save.bound = TT_EXACT;
    }

    trans_table[key] = save;
}
history.pop(state->hash());
return best_score;
}

/*============================================================
 * PVS — search
 *
 * Root search
 *============================================================*/
SearchResult submission::search(
    State* state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
) {
    ctx.reset();

    if (depth <= 0) {
        depth = 4;
    }

    uint64_t root_key = tt_key(state);
    if (depth <= 1 || root_key != last_root_key) {
        trans_table.clear();
        last_root_key = root_key;
    } else if (trans_table.size() > 1000000) {
        trans_table.clear();
    }

    SubmissionParams p = SubmissionParams::from_map(ctx.params);

    SearchResult result;
    result.depth = depth;

    if (state->legal_actions.empty()) {
        state->get_legal_actions();
    }

    int total_moves = (int)state->legal_actions.size();

    if (total_moves == 0) {
        result.score = 0;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        return result;
    }

    Move book_move;
    if (opening_book_move(state, book_move)) {
        result.best_move = book_move;
        result.score = state->evaluate(
            p.use_kp_eval,
            p.use_eval_mobility,
            &history
        );
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        result.pv.clear();
        result.pv.push_back(result.best_move);

        if (p.report_partial && ctx.on_root_update) {
            ctx.on_root_update({
                result.best_move,
                result.score,
                depth,
                1,
                total_moves
            });
        }

        return result;
    }

    std::vector<Move> actions = get_ordered_moves(state);

    int alpha = M_MAX;
    int beta = P_MAX;
    int best_score = M_MAX;
    int move_index = 0;
    bool first_child = true;

    for (const Move& action : actions) {
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int score;
        int rep_score = 0;

        bool is_repetition = repetition_draw_score(next, history, rep_score);

        if (is_repetition) {
            score = orient_child_score(rep_score, same);
            first_child = false;
        } else if (first_child) {
            score = search_child_pvs(
                next,
                same,
                depth - 1,
                alpha,
                beta,
                history,
                1,
                ctx,
                p
            );

            first_child = false;
        } else {
            score = search_child_pvs(
                next,
                same,
                depth - 1,
                alpha,
                alpha + 1,
                history,
                1,
                ctx,
                p
            );

            if (score > alpha && score < beta) {
                score = search_child_pvs(
                    next,
                    same,
                    depth - 1,
                    alpha,
                    beta,
                    history,
                    1,
                    ctx,
                    p
                );
            }
        }

        if (!is_repetition) {
            score += orient_child_score(
                repeated_position_bias(next, history),
                same
            );
            score -= root_repetition_penalty(next, history);
        }

        delete next;

        if (score > best_score) {
            best_score = score;
            result.best_move = action;

            if (p.report_partial && ctx.on_root_update) {
                ctx.on_root_update({
                    result.best_move,
                    best_score,
                    depth,
                    move_index + 1,
                    total_moves
                });
            }
        }

        if (score > alpha) {
            alpha = score;
        }

        move_index++;
    }

    result.score = best_score;
    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;

    result.pv.clear();
    result.pv.push_back(result.best_move);

    return result;
}

/*============================================================
 * PVS — default_params / param_defs
 *============================================================*/
ParamMap submission::default_params() {
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "false"},
    };
}

std::vector<ParamDef> submission::param_defs() {
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "false"},
    };
}
