#pragma once

#include <algorithm>
#include <utility>
#include <initializer_list>
#include <iterator>
#include <vector>

#include <util/compat.hpp>
#include <util/iterutil.hpp>
#include <util/rangeutil.hpp>

namespace msparse {

namespace util = nest::mc::util;

struct msparse_error: std::runtime_error {
    msparse_error(const std::string &what): std::runtime_error(what) {}
};

constexpr unsigned row_npos = unsigned(-1);

template <typename X>
class row {
public:
    struct entry {
        unsigned col;
        X value;
    };
    static constexpr unsigned npos = row_npos;

private:
    // entries must have strictly monotonically increasing col numbers.
    std::vector<entry> data_;

    bool check_invariant() const {
        for (unsigned i = 1; i<data_.size(); ++i) {
            if (data_[i].col<=data_[i-1].col) return false;
        }
        return true;
    }

public:
    row() = default;
    row(const row&) = default;

    row(std::initializer_list<entry> il): data_(il) {
        if (!check_invariant())
            throw msparse_error("improper row element list");
    }

    template <typename InIter>
    row(InIter b, InIter e): data_(b, e) {
        if (!check_invariant())
            throw msparse_error("improper row element list");
    }

    unsigned size() const { return data_.size(); }
    bool empty() const { return size()==0; }

    auto begin() -> decltype(data_.begin()) { return data_.begin(); }
    auto begin() const -> decltype(data_.cbegin()) { return data_.cbegin(); }
    auto end() -> decltype(data_.end()) { return data_.end(); }
    auto end() const -> decltype(data_.cend()) { return data_.cend(); }

    unsigned mincol() const {
        return empty()? npos: data_.front().col;
    }

    unsigned mincol_after(unsigned c) const {
        auto i = std::upper_bound(data_.begin(), data_.end(), c,
            [](unsigned a, const entry& b) { return a<b.col; });

        return i==data_.end()? npos: i->col;
    }

    unsigned maxcol() const {
        return empty()? npos: data_.back().col;
    }

    const entry& get(unsigned i) const {
        return data_[i];
    }

    void push_back(const entry& e) {
        if (!empty() && e.col <= data_.back().col)
            throw msparse_error("cannot push_back row elements out of order");
        data_.push_back(e);
    }

    unsigned index(unsigned c) const {
        auto i = std::lower_bound(data_.begin(), data_.end(), c,
            [](const entry& a, unsigned b) { return a.col<b; });

        return (i==data_.end() || i->col!=c)? npos: std::distance(data_.begin(), i);
    }

    // remove all entries from column c onwards
    void truncate(unsigned c) {
        auto i = std::lower_bound(data_.begin(), data_.end(), c,
            [](const entry& a, unsigned b) { return a.col<b; });
        data_.erase(i, data_.end());
    }

    X operator[](unsigned c) const {
        auto i = index(c);
        return i==npos? X{}: data_[i].value;
    }

    struct assign_proxy {
        row<X>& row_;
        unsigned c;

        assign_proxy(row<X>& r, unsigned c): row_(r), c(c) {}

        operator X() const { return const_cast<const row<X>&>(row_)[c]; }
        assign_proxy& operator=(const X& x) {
            auto i = std::lower_bound(row_.data_.begin(), row_.data_.end(), c,
                [](const entry& a, unsigned b) { return a.col<b; });

            if (i==row_.data_.end() || i->col!=c) {
                row_.data_.insert(i, {c, x});
            }
            else if (x == X{}) {
                row_.data_.erase(i);
            }
            else {
                i->value = x;
            }

            return *this;
        }
    };

    assign_proxy operator[](unsigned c) {
        return assign_proxy{*this, c};
    }

    template <typename RASeq>
    auto dot(const RASeq& v) const -> decltype(X{}*util::front(v)) {
        using result_type = decltype(X{}*util::front(v));
        result_type s{};

        auto nv = util::size(v);
        for (const auto& e: data_) {
            if (e.col>=nv) throw msparse_error("right multiplicand too short");
            s += e.value*v[e.col];
        }
        return s;
    }
};

template <typename X>
class matrix {
private:
    std::vector<row<X>> rows;
    unsigned cols = 0;
    unsigned aug = row_npos;

public:
    static constexpr unsigned npos = row_npos;

    matrix() = default;
    matrix(unsigned n, unsigned c): rows(n), cols(c) {}

    row<X>& operator[](unsigned i) { return rows[i]; }
    const row<X>& operator[](unsigned i) const { return rows[i]; }

    unsigned size() const { return rows.size(); }
    unsigned nrow() const { return size(); }
    unsigned ncol() const { return cols; }
    unsigned augcol() const { return aug; }

    bool empty() const { return size()==0; }
    bool augmented() const { return aug!=npos; }

    template <typename Seq>
    void augment(const Seq& col_dense) {
        unsigned r = 0;
        for (const auto& v: col_dense) {
            if (r>=rows.size()) throw msparse_error("augmented column size mismatch");
            rows[r++].push_back({cols, v});
        }
        if (aug==npos) aug=cols;
        ++cols;
    }

    void diminish() {
        if (aug==npos) return;
        for (auto& row: rows) row.truncate(aug);
        cols = aug;
        aug = npos;
    }
};

// sparse * dense vector muliply: writes A*x to b.
template <typename AT, typename RASeqX, typename SeqB>
void mul_dense(const matrix<AT>& A, const RASeqX& x, SeqB& b) {
    auto bi = std::begin(b);
    unsigned n = A.nrow();
    for (unsigned i = 0; i<n; ++i) {
        if (bi==compat::end(b)) throw msparse_error("output sequence b too short");
        *bi++ = A[i].dot(x);
    }
}

} // namespace msparse
