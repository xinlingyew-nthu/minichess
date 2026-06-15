// #include <utility>
// #include "state.hpp"
// #include "alphabeta.hpp"
// #include <algorithm>
// #include <vector>


// /*============================================================
//  * Alphabeta — eval_ctx
//  *
//  * Negamax without pruning. Caller manages memory.
//  *============================================================*/

//  static int move_score(State* state, const Move& m){
//     int from_r = m.first.first;
//     int to_r = m.second.first;
//     int to_c = m.second.second;

//     int captured = state->piece_at(1 - state->player, to_r, to_c);

//     static const int val[7] = {0, 2, 6, 7, 8, 20, 10000};

//     int score = 0;

//     if(captured){
//         score += 1000 + val[captured];
//     }

//     if(state->player == 0){
//         score += (from_r - to_r); // white 往上 row 變小
//     }else{
//         score += (to_r - from_r); // black 往下 row 變大
//     }

//     return score;
// }

// int Alphabeta::eval_ctx(
//     State *state,
//     int depth,
//     int alpha,
//     int beta,
//     GameHistory& history,
//     int ply,
//     SearchContext& ctx,
//     const ABParams& p
// ){
//     ctx.nodes++;
//     if(ply > ctx.seldepth){
//         ctx.seldepth = ply;
//     }
//     if(ctx.stop){
//         return 0;
//     }

//     /* === Lazy move generation (sets game_state) === */
//     if(state->legal_actions.empty() && state->game_state == UNKNOWN){
//         state->get_legal_actions();
//     }

//     /* === Terminal / leaf checks === */

//     // [ Hackathon TODO 3-1 ]
//     // return the score for a winning terminal state
//     // Hint: prefer faster wins by using ply.
//     if(state->game_state == WIN){
//         return P_MAX - ply; // max score for win, prefer faster wins
//     }

//     if(state->game_state == DRAW){
//         return 0;
//     }

//     /* === Repetition check (game-specific) === */
//     int rep_score;
//     if(state->check_repetition(history, rep_score)){
//         return rep_score;
//     }
//     history.push(state->hash());

//     if(depth <= 0){
//         int score = state->evaluate(
//             p.use_kp_eval, p.use_eval_mobility, &history
//         ); 
//         history.pop(state->hash());
//         return score;
//     }

//     /* === Negamax loop === */
//     int best_score = M_MAX;

//     std::vector<Move> actions = state->legal_actions;

//     std::sort(actions.begin(), actions.end(),
//         [state](const Move& a, const Move& b){
//             return move_score(state, a) > move_score(state, b);
//         }
//     );

//     for(auto& action : actions){
//         // [ Hackathon TODO 3-2 ]
//         // create the child state after applying action
//         State* next = state->next_state(action);//加沙我走action 產生下一個棋盤

//         bool same = next->same_player_as_parent();

//         // [Hackathon TODO 3-3]
//         // search the child one level deeper
//         int score = eval_ctx(next, depth - 1, -beta,-alpha,history, ply + 1, ctx, p);//對下一個棋盤評分

//         // [Hackathon TODO 3-4]
//         // convert raw to the current player's perspective.
//         int raw = same ? score : -score;

//         delete next;

//         // [ Hackathon TODO 3-5 ]
//         // update best_score if this child is better.
//         //找分數最高的
//         if(raw > best_score){
//             best_score = raw;
//         }

//         if(raw >alpha){
//             alpha =raw;
//         }
//         //後面可以省略 對手肯定會走
//         if(alpha >=beta){
//             break;
//         }

//     }

//     history.pop(state->hash());
//     return best_score;
// }


// /*============================================================
//  * Alphabeta — search
//  *
//  * Iterate legal moves, call eval_ctx, return SearchResult.
//  *============================================================*/
// SearchResult Alphabeta::search(
//     State *state,
//     int depth,
//     GameHistory& history,
//     SearchContext& ctx
// ){
//     ctx.reset();

//     if(depth <= 0){
//         depth = 3;
//     }

//     ABParams p = ABParams::from_map(ctx.params);
//     SearchResult result;
//     result.depth = depth;

//     if(!state->legal_actions.size()){
//         state->get_legal_actions();
//     }


//     int best_score = M_MAX - 10;
//     int move_index = 0;
//     int total_moves = (int)state->legal_actions.size();
//     //避免沒有合法步時 best_move 沒設定。
//     if(total_moves == 0){
//         result.score = 0;
//         result.nodes = ctx.nodes;
//         result.seldepth = ctx.seldepth;
//         return result;
//     }
//     int alpha=M_MAX;
//     int beta=P_MAX;
//     std::vector<Move> actions = state->legal_actions;

//     std::sort(actions.begin(), actions.end(),
//         [state](const Move& a, const Move& b){
//             return move_score(state, a) > move_score(state, b);
//         }
//     );

//     for(auto& action : actions){
//         /* [ Hackathon TODO 4-1 ]
//          * search this move like TODO 3, but starting from the root */
//         State* next = state->next_state(action);
//         bool same = next->same_player_as_parent();
//         int child_score = eval_ctx(next, depth - 1, -beta,-alpha,history, 1, ctx , p);
//         int score = same ? child_score : -child_score;
//         delete next;

//             if(score > best_score){
//                 // [ Hackathon TODO 4-2 ]
//                 // keep this move if it is the best so far
//                 best_score = score;
//                 result.best_move = action;

//                 if(score > alpha){
//                     alpha = score;
//                 }

//                 if(p.report_partial && ctx.on_root_update){
//                    ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
//                 }
//             }  
//         move_index++;
//     }

//     // [ Hackathon TODO 4-3 ]
//     // update result and return
//         result.score = best_score;
//         result.nodes = ctx.nodes;
//         result.seldepth = ctx.seldepth;

//         result.pv.clear();
//         if(total_moves > 0){
//             result.pv.push_back(result.best_move);
//         }


//         return result;
// } 


// /*============================================================
//  * Alphabeta — default_params / param_defs
//  *============================================================*/
// ParamMap Alphabeta::default_params(){
//     return {
//         {"UseKPEval", "true"},
//         {"UseEvalMobility", "true"},
//         {"ReportPartial", "true"},
//     };
// }

// std::vector<ParamDef> Alphabeta::param_defs(){
//     return {
//         {"UseKPEval", ParamDef::CHECK, "true"},
//         {"UseEvalMobility", ParamDef::CHECK, "true"},
//         {"ReportPartial", ParamDef::CHECK, "true"},
//     };
// }
