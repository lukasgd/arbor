#include <iostream>
#include <stdexcept>
#include <map>
#include <vector>

#include <arbor/morph/morphexcept.hpp>
#include <arbor/morph/segment_tree.hpp>

#include "io/sepval.hpp"
#include "util/span.hpp"
#include "util/transform.hpp"

using arb::util::make_span;

namespace arb {

struct node_t {
    msize_t parent;
    msize_t id;
};

using node_p = std::function<bool(const node_t&)>;

node_p yes = [](const node_t&) { return true; };

// invert parent <*> child relation, returns a map of parent_id -> [children_id]
// For predictable ordering we sort the vectors.
std::map<msize_t, std::vector<msize_t>> tree_to_children(const segment_tree& tree) {
    const auto& parents = tree.parents();
    std::map<msize_t, std::vector<msize_t>> result;
    for (msize_t ix = 0; ix < tree.size(); ++ix) result[parents[ix]].push_back(ix);
    for (auto& [k, v]: result) std::sort(v.begin(), v.end());
    return result;
}
    
// Copy a segment tree into a new tree
// - tree to be (partially) copied
// - start={parent, id}: start by attaching segment=`id` from `tree` to the segment
//                       `parent` of `init`, then its children to it recursively
// - predicate: if returning false for a given node, we prune that sub-tree starting
//              at node (inclusive). Can be used to prune trees by parent or id.
// - init: initial tree to append to
// Note: this is an iterative implementation of depth-first traversal with an explicit stack.
segment_tree copy_subtree_if(const segment_tree& tree,
                             const node_t& start,
                             node_p predicate,
                             const segment_tree& init={}) {
    auto children_of = tree_to_children(tree);
    auto& segments = tree.segments();
    segment_tree result = init;
    auto todo = std::vector<node_t>{start};
    while (!todo.empty()) {
        auto node = todo.back();
        todo.pop_back();
        if (!predicate(node)) continue;
        const auto& segment = segments[node.id];
        auto current = result.append(node.parent, segment.prox, segment.dist, segment.tag);
        for (auto child: children_of[node.id]) {
            todo.push_back({current, child});
        }
    }
    return result;
}

std::pair<segment_tree, segment_tree>
split_at(const segment_tree& tree, msize_t at) {
    if (at >= tree.size() || at == mnpos) throw invalid_segment_parent(at, tree.size());
    // span the sub-tree starting at the splitting node
    segment_tree post = copy_subtree_if(tree, {mnpos, at}, yes);

    // copy the original segment_tree (as a graph), but skip all nodes in the `post` subtree
    auto segment_ids = make_span(0, tree.segments().size());
    std::vector<msize_t> roots;
    std::copy_if(segment_ids.begin(),
                 segment_ids.end(),
                 std::back_inserter(roots),
                 [&tree](msize_t i) { return tree.is_root(i); });

    segment_tree pre;
    for (auto root : roots) {
        pre = copy_subtree_if(tree,
                              {mnpos, root},
                              [=](auto& node) { return node.id != at; },
                              pre);
    }
    return {pre, post};
}

segment_tree
join_at(const segment_tree& lhs, msize_t at, const segment_tree& rhs) {
    if (at >= lhs.size() && at != mnpos) throw invalid_segment_parent(at, lhs.size());
    return copy_subtree_if(rhs, {at, 0}, yes, lhs);
}

bool
equivalent(const segment_tree& a,
           const segment_tree& b) {
    if(a.size() != b.size()) return false;

    auto
        a_children_of = tree_to_children(a),
        b_children_of = tree_to_children(b);

    auto fetch_children = [&](auto cursor, const auto& segments, auto& children_of) {
        std::vector<arb::msegment> segs;
        for (auto ix: children_of[cursor]) segs.push_back(segments[ix]);
        std::sort(segs.begin(), segs.end(),
                  [](auto l, auto r) {
                      l.id = r.id = 0;
                      return l < r;
                  });
        return segs;
    };

    std::vector<std::pair<arb::msize_t, arb::msize_t>> todo{{arb::mnpos, arb::mnpos}};
    while (!todo.empty()) {
        const auto& [a_cursor, b_cursor] = todo.back();
        auto as = fetch_children(a_cursor, a.segments(), a_children_of);
        auto bs = fetch_children(b_cursor, b.segments(), b_children_of);
        todo.pop_back();
        if (as.size() != bs.size()) return false;
        for (msize_t ix = 0; ix < as.size(); ++ix) {
            if ((as[ix].prox != bs[ix].prox) ||
                (as[ix].dist != bs[ix].dist) ||
                (as[ix].tag != bs[ix].tag)) return false;
            todo.emplace_back(as[ix].id, bs[ix].id);
        }
    }
    return true;
}

segment_tree
apply(const segment_tree& tree, const isometry& iso) {
    auto result = tree;
    for (auto& seg: result.segments_) {
        seg.prox = iso.apply(seg.prox);
        seg.dist = iso.apply(seg.dist);
    }
    return result;
}

void segment_tree::reserve(msize_t n) {
    segments_.reserve(n);
    parents_.reserve(n);
    seg_children_.reserve(n);
}

msize_t segment_tree::append(msize_t p, const mpoint& prox, const mpoint& dist, int tag) {
    if (p>=size() && p!=mnpos) {
        throw invalid_segment_parent(p, size());
    }

    auto id = size();
    segments_.push_back(msegment{id, prox, dist, tag});
    parents_.push_back(p);

    // Set the point properties for the new point, and update those of the parent.
    seg_children_.push_back({});
    if (p!=mnpos) {
        seg_children_[p].increment();
    }

    return id;
}

msize_t segment_tree::append(msize_t p, const mpoint& dist, int tag) {
    // If attaching to the root both prox and dist ends must be specified.
    if (p==mnpos) {
        throw invalid_segment_parent(p, size());
    }
    if (p>=size()) {
        throw invalid_segment_parent(p, size());
    }
    return append(p, segments_[p].dist, dist, tag);
}

msize_t segment_tree::size() const {
    return segments_.size();
}

bool segment_tree::empty() const {
    return segments_.empty();
}

const std::vector<msegment>& segment_tree::segments() const {
    return segments_;
}

const std::vector<msize_t>& segment_tree::parents() const {
    return parents_;
}

bool segment_tree::is_fork(msize_t i) const {
    if (i>=size()) throw no_such_segment(i);
    return seg_children_[i].is_fork();
}
bool segment_tree::is_terminal(msize_t i) const {
    if (i>=size()) throw no_such_segment(i);
    return seg_children_[i].is_terminal();
}
bool segment_tree::is_root(msize_t i) const {
    if (i>=size()) throw no_such_segment(i);
    return parents_[i]==mnpos;
}

ARB_ARBOR_API std::ostream& operator<<(std::ostream& o, const segment_tree& m) {
    auto tstr = util::transform_view(m.parents_,
            [](msize_t i) -> std::string {
                return i==mnpos? "npos": std::to_string(i);
            });
    bool one_line = m.size()<2u;
    return o << "(segment_tree (" << (one_line? "": "\n  ") << io::sepval(m.segments_, "\n  ")
             << (one_line? ") (": ")\n  (")
             << io::sepval(tstr, ' ') <<  "))";
}


ARB_ARBOR_API std::pair<segment_tree, std::vector<msize_t>> prune_tag(const segment_tree& in, int tag) {
    const auto& in_segments = in.segments();
    const auto& in_parents = in.parents();
    segment_tree out;

    std::vector<int> pruned_id_upper_bounds, pruned_id_offsets;
    std::vector<msize_t> tag_roots;

    int num_pruned = 0;
    for (auto i: make_span(0, in_segments.size())) {
        if (in_segments[i].tag == tag) {
            ++num_pruned;

            // Get roots of pruned tag region
            auto par = in_parents[i];
            if (in_segments[i].tag == tag && (par == mnpos || in_segments[par].tag != tag)) {
                tag_roots.push_back(i);
            }

            if (i+1 < in_segments.size() && in_segments[i+1].tag != tag) {
                pruned_id_upper_bounds.push_back(i+1);
                pruned_id_offsets.push_back(num_pruned);
            }
        }
    }

    for (auto i: make_span(in_segments.size())) {
        const auto& seg = in_segments[i];
        auto par = in_parents[i];

        if (seg.tag != tag) {
            if (par != mnpos && in_segments[par].tag == tag) {
                // children of pruned parents must be pruned
                throw unpruned_child(par, seg.id, tag);
            } else {
                if (par != mnpos) {
                    auto ui = upper_bound(pruned_id_upper_bounds.begin(), 
                                          pruned_id_upper_bounds.end(), 
                                          par) - pruned_id_upper_bounds.begin();
                    par -= ui > 0 ? pruned_id_offsets[ui-1] : 0;
                }
                out.append(par, seg.prox, seg.dist, seg.tag);
            }
        }
    }

    return {out, tag_roots};
}


ARB_ARBOR_API std::vector<msize_t> tag_roots(const segment_tree& t, int tag) {
    const auto& segments = t.segments();
    const auto& parents = t.parents();
    std::vector<msize_t> tag_roots;

    for (auto i: make_span(0, segments.size())) {
        auto par = parents[i];
        if (segments[i].tag == tag && (par == mnpos || segments[par].tag != tag)) {
            tag_roots.push_back(i);
        }
    }

    return tag_roots;
}


} // namespace arb

