#include <utility>
#include <algorithm>
#include <vector>

#include "state.hpp"
#include "114062262_alphabeta.hpp"

/*============================================================
 * Move ordering helper
 * 越可能是好棋，分數越高，越早搜尋
 *============================================================*/
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

    // 1. capture 優先，吃越貴越好
    if (captured) {
        score += 100000 + val[captured] * 10;

        if (moving) {
            score -= val[moving];
        }

        if (captured == 1) {
            int danger = 0;

            if (state->player == 0) {
                danger = to_r;
            } else {
                danger = BOARD_H - 1 - to_r;
            }

            score += danger * danger * 200;
        }
    }

    // 2. pawn 前進稍微加分
    if (moving == 1) {
        if (state->player == 0) {
            score += (from_r - to_r) * 50;  // white row 變小
        } else {
            score += (to_r - from_r) * 50;  // black row 變大
        }
    }

    // 3. 中央位置稍微加分
    score += 5 - abs(to_c - 2);

    return score;
}

/*============================================================
 * AlphaBeta — eval_ctx
 *
 * Negamax + Alpha-Beta pruning
 * 回傳值永遠是「目前 state->player 角度」的分數
 *============================================================*/
int Alphabeta::eval_ctx(
    State* state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const ABParams& p
) {
    ctx.nodes++;
    if (ply > ctx.seldepth) {
        ctx.seldepth = ply;
    }

    if (ctx.stop) {
        return 0;
    }

    // Lazy move generation
    if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
        state->get_legal_actions();
    }

    // Terminal states
    if (state->game_state == WIN) {
        return P_MAX - ply;  // 越快贏越好
    }

    if (state->game_state == DRAW) {
        return -30;
    }

    // Repetition
    int rep_score = 0;
    if (state->check_repetition(history, rep_score)) {
        return -80;
    }

    history.push(state->hash());

    // Leaf node
    if (depth <= 0) {
        int score = state->evaluate(
            p.use_kp_eval,
            p.use_eval_mobility,
            &history
        );

        history.pop(state->hash());
        return score;
    }

    std::vector<Move> actions = state->legal_actions;

    std::sort(actions.begin(), actions.end(),
        [state](const Move& a, const Move& b) {
            return move_score(state, a) > move_score(state, b);
        }
    );

    int best_score = M_MAX;

    for (const Move& action : actions) {
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int child_score;

        if (same) {
            // 如果還是同一個 player，就不用 negamax 反號
            child_score = eval_ctx(
                next,
                depth - 1,
                alpha,
                beta,
                history,
                ply + 1,
                ctx,
                p
            );
        } else {
            // 換對手，所以 window 反過來，分數也反號
            child_score = -eval_ctx(
                next,
                depth - 1,
                -beta,
                -alpha,
                history,
                ply + 1,
                ctx,
                p
            );
        }

        delete next;

        if (child_score > best_score) {
            best_score = child_score;
        }

        if (child_score > alpha) {
            alpha = child_score;
        }

        if (alpha >= beta) {
            break;
        }
    }

    history.pop(state->hash());
    return best_score;
}

/*============================================================
 * AlphaBeta — search
 *
 * Root search
 *============================================================*/
SearchResult Alphabeta::search(
    State* state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
) {
    ctx.reset();

    if (depth <= 0) {
        depth = 4;
    }

    ABParams p = ABParams::from_map(ctx.params);

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

    std::vector<Move> actions = state->legal_actions;

    std::sort(actions.begin(), actions.end(),
        [state](const Move& a, const Move& b) {
            return move_score(state, a) > move_score(state, b);
        }
    );

    int alpha = M_MAX;
    int beta = P_MAX;
    int best_score = M_MAX;
    int move_index = 0;

    for (const Move& action : actions) {
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int score;

        if (same) {
            score = eval_ctx(
                next,
                depth - 1,
                alpha,
                beta,
                history,
                1,
                ctx,
                p
            );
        } else {
            score = -eval_ctx(
                next,
                depth - 1,
                -beta,
                -alpha,
                history,
                1,
                ctx,
                p
            );
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
 * AlphaBeta — default_params / param_defs
 *============================================================*/
ParamMap Alphabeta::default_params() {
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> Alphabeta::param_defs() {
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
