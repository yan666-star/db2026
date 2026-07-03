/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "ix_index_handle.h"

#include "ix_scan.h"

/**
 * @brief 在当前node中查找第一个>=target的key_idx
 *
 * @return key_idx，范围为[0,num_key)，如果返回的key_idx=num_key，则表示target大于最后一个key
 * @note 返回key index（同时也是rid index），作为slot no
 */
int IxNodeHandle::lower_bound(const char *target) const
{
    int left = 0, right = page_hdr->num_key;
    while (left < right)
    {
        int mid = left + ((right - left) >> 1);
        if (ix_compare(get_key(mid), target, file_hdr->col_types_, file_hdr->col_lens_) < 0)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

/**
 * @brief 在当前node中查找第一个>target的key_idx
 *
 * @return key_idx，范围为[1,num_key)，如果返回的key_idx=num_key，则表示target大于等于最后一个key
 * @note 注意此处的范围从1开始
 */
int IxNodeHandle::upper_bound(const char *target) const
{
    int left = 0, right = page_hdr->num_key;
    while (left < right)
    {
        int mid = left + ((right - left) >> 1);
        if (ix_compare(get_key(mid), target, file_hdr->col_types_, file_hdr->col_lens_) <= 0)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

/**
 * @brief 用于叶子结点根据key来查找该结点中的键值对
 * 值value作为传出参数，函数返回是否查找成功
 *
 * @param key 目标key
 * @param[out] value 传出参数，目标key对应的Rid
 * @return 目标key是否存在
 */
bool IxNodeHandle::leaf_lookup(const char *key, Rid **value)
{
    auto index = lower_bound(key);
    if (index < page_hdr->num_key &&memcmp(get_key(index), key, file_hdr->col_tot_len_) == 0)
    {
        *value = get_rid(index);
        return true;
    }
    return false;
}

/**
 * 用于内部结点（非叶子节点）查找目标key所在的孩子结点（子树）
 * @param key 目标key
 * @return page_id_t 目标key所在的孩子节点（子树）的存储页面编号
 */
page_id_t IxNodeHandle::internal_lookup(const char *key)
{
    int index = upper_bound(key);
    return value_at(index);
}

/**
 * @brief 在指定位置插入n个连续的键值对
 * 将key的前n位插入到原来keys中的pos位置；将rid的前n位插入到原来rids中的pos位置
 *
 * @param pos 要插入键值对的位置
 * @param (key, rid) 连续键值对的起始地址，也就是第一个键值对，可以通过(key, rid)来获取n个键值对
 * @param n 键值对数量
 */
void IxNodeHandle::insert_pairs(int pos, const char *key, const Rid *rid, int n)
{
    assert(pos >= 0 && pos <= page_hdr->num_key && n >= 0);

    // move existing keys and rids to make space for new pairs
    int keys_to_move = page_hdr->num_key - pos;
    if (keys_to_move > 0) {
        // move keys
        char *key_start = keys + pos * file_hdr->col_tot_len_;
        memmove(key_start + n * file_hdr->col_tot_len_, key_start, keys_to_move * file_hdr->col_tot_len_);

        // move rids
        if (is_leaf_page()) {
            char *rid_start = (char *)rids + pos * sizeof(Rid);
            memmove(rid_start + n * sizeof(Rid), rid_start, keys_to_move * sizeof(Rid));
        } else { // internal node
            char *rid_start = (char *)rids + (pos + 1) * sizeof(Rid);
            memmove(rid_start + n * sizeof(Rid), rid_start, (keys_to_move) * sizeof(Rid));
        }
    }

    // insert new pairs
    memcpy(keys + pos * file_hdr->col_tot_len_, key, n * file_hdr->col_tot_len_);
    if (is_leaf_page()) {
        memcpy((char *)rids + pos * sizeof(Rid), rid, n * sizeof(Rid));
    } else { // internal node
        memcpy((char *)rids + (pos + 1) * sizeof(Rid), rid, n * sizeof(Rid));
    }

    page_hdr->num_key += n;
}

/**
 * @brief 用于在结点中插入单个键值对。
 * 函数返回插入后的键值对数量
 *
 * @param (key, value) 要插入的键值对
 * @return int 键值对数量
 */
int IxNodeHandle::insert(const char *key, const Rid &value)
{
    int pos = lower_bound(key);
    if (pos >= page_hdr->num_key)
    {
        insert_pairs(pos, key, &value, 1);
        return page_hdr->num_key;
    }
    if (memcmp(get_key(pos), key, file_hdr->col_tot_len_) != 0)
        insert_pairs(pos, key, &value, 1);
    return page_hdr->num_key;
}

/**
 * @brief 用于在结点中的指定位置删除单个键值对
 *
 * @param pos 要删除键值对的位置
 */
void IxNodeHandle::erase_pair(int pos)
{
    if (pos < 0 || pos >= page_hdr->num_key)
    {
        assert(false && "Invalid position in erase_pair");
        return;
    }

    // move keys
    int keys_to_move = page_hdr->num_key - pos - 1;
    if (keys_to_move > 0) {
        char *key_start = keys + pos * file_hdr->col_tot_len_;
        memmove(key_start, key_start + file_hdr->col_tot_len_,
                keys_to_move * file_hdr->col_tot_len_);
    }

    // move rids
    if (is_leaf_page()) {
        char *rid_start = (char *)rids + pos * sizeof(Rid);
        if (keys_to_move > 0) { // only move if there are keys to move
             memmove(rid_start, rid_start + sizeof(Rid), keys_to_move * sizeof(Rid));
        }
    } else { // internal node
        // delete key at pos, and rid at pos+1
        int rids_to_move = page_hdr->num_key - pos - 1;
        if (rids_to_move >= 0) {
             char *rid_start = (char *)rids + (pos + 1) * sizeof(Rid);
             memmove(rid_start, rid_start + sizeof(Rid), rids_to_move * sizeof(Rid));
        }
    }

    --page_hdr->num_key;
}

/**
 * @brief 用于在结点中删除指定key的键值对。函数返回删除后的键值对数量
 *
 * @param key 要删除的键值对key值
 * @return 完成删除操作后的键值对数量
 */
int IxNodeHandle::remove(const char *key)
{
    int pos = lower_bound(key);
    if (pos < page_hdr->num_key && ix_compare(get_key(pos), key, file_hdr->col_types_, file_hdr->col_lens_) == 0)
        erase_pair(pos);
    return page_hdr->num_key;
}

IxIndexHandle::IxIndexHandle(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager, int fd)
    : disk_manager_(disk_manager), buffer_pool_manager_(buffer_pool_manager), fd_(fd)
{
    char *buf = new char[PAGE_SIZE];
    memset(buf, 0, PAGE_SIZE);
    disk_manager_->read_page(fd, IX_FILE_HDR_PAGE, buf, PAGE_SIZE);
    file_hdr_ = new IxFileHdr();
    file_hdr_->deserialize(buf);
    
    delete[] buf;
    
    disk_manager_->set_fd2pageno(fd, file_hdr_->num_pages_);
}

void IxIndexHandle::flush_file_header() const {
    std::vector<char> data(file_hdr_->tot_len_);
    file_hdr_->serialize(data.data());
    disk_manager_->write_page(fd_, IX_FILE_HDR_PAGE, data.data(), file_hdr_->tot_len_);
}

/**
 * @brief 用于查找指定键所在的叶子结点
 * @param key 要查找的目标key值
 * @param operation 查找到目标键值对后要进行的操作类型
 * @param transaction 事务参数，如果不需要则默认传入nullptr
 * @return [leaf node] and [root_is_latched] 返回目标叶子结点以及根结点是否加锁
 */
std::pair<IxNodeHandle *, bool> IxIndexHandle::find_leaf_page(const char *key, Operation operation,
                                                              Transaction *transaction, bool find_first)
{
    if (file_hdr_->root_page_ == INVALID_PAGE_ID) {
        return {nullptr, false};
    }
    auto node = fetch_node(file_hdr_->root_page_);
    while (!node->is_leaf_page())
    {
        page_id_t child_page = node->internal_lookup(key);
        auto child = fetch_node(child_page);
        buffer_pool_manager_->unpin_page(node->get_page_id(), false);
        delete node;
        node = child;
    }
    return std::make_pair(node, false);
}

/**
 * @brief 用于查找指定键在叶子结点中的对应的值result
 *
 * @param key 查找的目标key值
 * @param result 用于存放结果的容器
 * @param transaction 事务指针
 * @return bool 返回目标键值对是否存在
 */
bool IxIndexHandle::get_value(const char *key, std::vector<Rid> *result, Transaction *transaction)
{
    if (file_hdr_->root_page_ == INVALID_PAGE_ID) {
        return false;
    }
    auto [leaf_node, root_is_latched] = find_leaf_page(key, Operation::FIND, transaction);
    if (leaf_node == nullptr) {
        return false;
    }
    Rid *rid = nullptr;
    bool is_success = leaf_node->leaf_lookup(key, &rid);
    if (is_success)
        result->push_back(*rid);
    buffer_pool_manager_->unpin_page(leaf_node->get_page_id(), false);
    delete leaf_node;
    return is_success;
}

/**
 * @brief  将传入的一个node拆分(Split)成两个结点，在node的右边生成一个新结点new node
 * @param node 需要拆分的结点
 * @return 拆分得到的new_node
 */
IxNodeHandle *IxIndexHandle::split(IxNodeHandle *node)
{
    IxNodeHandle *new_node = create_node();
    new_node->page_hdr->is_leaf = node->page_hdr->is_leaf;
    new_node->page_hdr->parent = node->page_hdr->parent;
    int old_size = node->get_size();
    int mid = old_size >> 1;
    int move_count = old_size - mid;
    new_node->insert_pairs(0, node->get_key(mid), node->get_rid(mid), move_count);
    node->page_hdr->num_key = mid;
    if (new_node->is_leaf_page())
    {
        new_node->page_hdr->prev_leaf = node->get_page_no();
        new_node->page_hdr->next_leaf = node->page_hdr->next_leaf;
        node->page_hdr->next_leaf = new_node->get_page_no();
        if (new_node->page_hdr->next_leaf != IX_NO_PAGE)
        {
        auto next_node = fetch_node(new_node->page_hdr->next_leaf);
        next_node->page_hdr->prev_leaf = new_node->get_page_no();
        buffer_pool_manager_->unpin_page(next_node->get_page_id(), true);
        delete next_node;
        }
        else
        {
            file_hdr_->last_leaf_ = new_node->get_page_no();
        }
    }
    else
    {
        for (int i = 0; i < new_node->get_size(); ++i)
        {
            maintain_child(new_node, i);
        }
    }
    return new_node;
}

IxNodeHandle *IxIndexHandle::split_internal(IxNodeHandle *node, char *promote_key)
{
    IxNodeHandle *new_node = create_node();
    new_node->page_hdr->is_leaf = false;
    new_node->page_hdr->parent = node->page_hdr->parent;
    
    int old_size = node->get_size();
    int mid = old_size >> 1;
    
    // move half of the keys and rids to new_node
    memcpy(promote_key, node->get_key(mid), file_hdr_->col_tot_len_);
    
    int new_node_key_count = old_size - mid - 1;
    
    // copy keys to new_node
    new_node->insert_pairs(0, node->get_key(mid + 1), node->get_rid(mid + 2), new_node_key_count);
    new_node->set_rid(0, *node->get_rid(mid + 1));
    
    new_node->page_hdr->num_key = new_node_key_count;
    node->page_hdr->num_key = mid;
    
    // maintain children's parent
    for (int i = 0; i <= new_node->get_size(); i++) {
        maintain_child(new_node, i);
    }
    
    return new_node;
}


/**
 * @brief Insert key & value pair into internal page after split
 * 拆分(Split)后，向上找到old_node的父结点
 * 将new_node的第一个key插入到父结点，其位置在 父结点指向old_node的孩子指针 之后
 * 如果插入后>=maxsize，则必须继续拆分父结点，然后在其父结点的父结点再插入，即需要递归
 * 直到找到的old_node为根结点时，结束递归（此时将会新建一个根R，关键字为key，old_node和new_node为其孩子）
 *
 * @param (old_node, new_node) 原结点为old_node，old_node被分裂之后产生了新的右兄弟结点new_node
 * @param key 要插入parent的key
 */
void IxIndexHandle::insert_into_parent(IxNodeHandle *old_node, const char *key, IxNodeHandle *new_node,
                                       Transaction *transaction)
{
    if (old_node->is_root_page())
    {
        auto new_root = create_node();
        new_root->page_hdr->is_leaf = false;
        new_root->page_hdr->parent = INVALID_PAGE_ID;
        new_root->page_hdr->num_key = 1;
        
        new_root->set_key(0, key);
        new_root->set_rid(0, {old_node->get_page_no(), -1});
        new_root->set_rid(1, {new_node->get_page_no(), -1});
        
        old_node->set_parent_page_no(new_root->get_page_no());
        new_node->set_parent_page_no(new_root->get_page_no());
        file_hdr_->root_page_ = new_root->get_page_no();
        
        buffer_pool_manager_->unpin_page(new_root->get_page_id(), true);
        delete new_root;
        return;
    }
    auto parent = fetch_node(old_node->get_parent_page_no());
    int index = parent->upper_bound(key);
    Rid new_node_rid = {new_node->get_page_no(), -1};
    parent->insert_pairs(index, key, &new_node_rid, 1);
    
    if (parent->get_size() >= parent->get_max_size())
    {
        char * promote_key=new char[file_hdr_->col_tot_len_];
        auto spilt_parent = split_internal(parent, promote_key);
        insert_into_parent(parent, promote_key, spilt_parent, transaction);
        buffer_pool_manager_->unpin_page(spilt_parent->get_page_id(), true);
        delete []promote_key;
        delete spilt_parent;
    }
    buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
    delete parent;
}

/**
 * @brief 将指定键值对插入到B+树中
 * @param (key, value) 要插入的键值对
 * @param transaction 事务指针
 * @return page_id_t 插入到的叶结点的page_no
 */
page_id_t IxIndexHandle::insert_entry(const char *key, const Rid &value, Transaction *transaction)
{
    std::scoped_lock<std::mutex> lock(root_latch_);
    auto [leaf_node, root_is_latched] = find_leaf_page(key, Operation::INSERT, transaction);
    if (leaf_node == nullptr) {
        leaf_node = create_node();
        leaf_node->page_hdr->is_leaf = true;
        leaf_node->page_hdr->parent = IX_NO_PAGE;
        leaf_node->page_hdr->num_key=0;
        leaf_node->page_hdr->next_leaf=INVALID_PAGE_ID;
        file_hdr_->root_page_ = leaf_node->get_page_no();
        file_hdr_->first_leaf_ = leaf_node->get_page_no();
        file_hdr_->last_leaf_ = leaf_node->get_page_no();
    }
    int before_size = leaf_node->get_size();
    leaf_node->insert(key, value);
    bool is_inserted = before_size < leaf_node->get_size();
    if (leaf_node->get_size() >= leaf_node->get_max_size() && is_inserted)
    {
        auto new_node = split(leaf_node);
        if (file_hdr_->last_leaf_ == leaf_node->get_page_no())
        {
            file_hdr_->last_leaf_ = new_node->get_page_no();
        }
        insert_into_parent(leaf_node, new_node->get_key(0), new_node, transaction);
        buffer_pool_manager_->unpin_page(new_node->get_page_id(), is_inserted);
        delete new_node;
    }
    buffer_pool_manager_->unpin_page(leaf_node->get_page_id(), is_inserted);
    delete leaf_node;
    return 0;
}

/**
 * @brief 用于删除B+树中含有指定key的键值对
 * @param key 要删除的key值
 * @param transaction 事务指针
 */
bool IxIndexHandle::delete_entry(const char *key, Transaction *transaction)
{
    std::scoped_lock<std::mutex> lock(root_latch_);
    auto [node, root_is_latched_ignored] = find_leaf_page(key, Operation::DELETE, transaction);
    if (node == nullptr) {
        return false;
    }
    int key_idx = node->lower_bound(key);
    if (key_idx >= node->get_size() || ix_compare(node->get_key(key_idx), key, file_hdr_->col_types_, file_hdr_->col_lens_) != 0) {
        buffer_pool_manager_->unpin_page(node->get_page_id(), false);
        delete node;
        return false;
    }
    node->erase_pair(key_idx);
    if (key_idx == 0 && node->get_size() > 0 && !node->is_root_page()) {
        // Update the parent key if the first key in a leaf node is deleted.
        auto parent = fetch_node(node->get_parent_page_no());
        int index_in_parent = parent->find_child(node);
        if (index_in_parent > 0) {
            parent->set_key(index_in_parent - 1, node->get_key(0));
        }
        buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        delete parent;
    }
    bool root_is_latched = false;
    auto is_coalesce_or_redistribute_succ = coalesce_or_redistribute(node, transaction, &root_is_latched);
    if (is_coalesce_or_redistribute_succ)
    {
        if (transaction != nullptr)
            transaction->append_index_deleted_page(node->page);
    }
    buffer_pool_manager_->unpin_page(node->get_page_id(), true);
    delete node;
    return true;
}

/**
 * @brief 用于处理合并和重分配的逻辑，用于删除键值对后调用
 *
 * @param node 执行完删除操作的结点
 * @param transaction 事务指针
 * @param root_is_latched 传出参数：根节点是否上锁，用于并发操作
 * @return 是否需要删除结点
 */
bool IxIndexHandle::coalesce_or_redistribute(IxNodeHandle *node, Transaction *transaction, bool *root_is_latched)
{
    if (node->is_root_page())
        return adjust_root(node);
    else if (node->get_size() >= (node->get_max_size() >> 1))
        return false;

    auto parent = fetch_node(node->get_parent_page_no());
    auto index = parent->find_child(node);
    IxNodeHandle *sibling_node = nullptr;

    if (index > 0) {
        sibling_node = fetch_node(parent->value_at(index - 1));
    } else {
        sibling_node = fetch_node(parent->value_at(1));
    }

    bool is_success = false;
    if (node->get_size() + sibling_node->get_size() >= sibling_node->get_max_size()) {
        if (node->is_leaf_page()) {
            redistribute(sibling_node, node, parent, index);
        } else {
            redistribute_internal(sibling_node, node, parent, index);
        }
    } else {
        if (node->is_leaf_page()) {
            if (index == 0) {
                is_success = coalesce(&node, &sibling_node, &parent, 1, transaction, root_is_latched);
            } else {
                is_success = coalesce(&sibling_node, &node, &parent, index, transaction, root_is_latched);
            }
        } else {
            if (index == 0) {
                is_success = coalesce_internal(&node, &sibling_node, &parent, 1, transaction, root_is_latched);
            } else {
                is_success = coalesce_internal(&sibling_node, &node, &parent, index, transaction, root_is_latched);
            }
        }
    }

    buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
    buffer_pool_manager_->unpin_page(sibling_node->get_page_id(), true);
    delete parent;
    delete sibling_node;
    return is_success;
}

/**
 * @brief 用于当根结点被删除了一个键值对之后的处理
 * @param old_root_node 原根节点
 * @return bool 根结点是否需要被删除
 */
bool IxIndexHandle::adjust_root(IxNodeHandle *old_root_node)
{
    bool is_success = false;
    if (old_root_node->is_leaf_page() && old_root_node->get_size() == 0)
    {
        file_hdr_->first_leaf_ = old_root_node->get_page_no();
        file_hdr_->last_leaf_ = old_root_node->get_page_no();
        return false;
    }
    else if (!old_root_node->is_leaf_page() && old_root_node->get_size() == 0)
    {
        auto new_root = fetch_node(old_root_node->value_at(0));
        new_root->set_parent_page_no(INVALID_PAGE_ID);
        file_hdr_->root_page_ = new_root->get_page_no();
        buffer_pool_manager_->unpin_page(new_root->get_page_id(), true);
        delete new_root;
        release_node_handle(*old_root_node);
        is_success = true;
    }

    return is_success;
}

/**
 * @brief 重新分配node和兄弟结点neighbor_node的键值对
 * @param neighbor_node sibling page of input "node"
 * @param node input from method coalesceOrRedistribute()
 * @param parent the parent of "node" and "neighbor_node"
 * @param index node在parent中的rid_idx
 */
void IxIndexHandle::redistribute(IxNodeHandle *neighbor_node, IxNodeHandle *node, IxNodeHandle *parent, int index)
{
    if (index == 0)
    {
        node->insert_pairs(node->get_size(), neighbor_node->get_key(0), neighbor_node->get_rid(0), 1);
        neighbor_node->erase_pair(0);
        parent->set_key(0, neighbor_node->get_key(0));
        maintain_child(node, node->get_size() - 1);
    }
    else
    {
        node->insert_pairs(0, neighbor_node->get_key(neighbor_node->get_size() - 1), neighbor_node->get_rid(neighbor_node->get_size() - 1), 1);
        neighbor_node->erase_pair(neighbor_node->get_size() - 1);
        parent->set_key(index - 1, node->get_key(0));
        maintain_child(node, 0);
    }
}

/**
 * @brief 合并(Coalesce)函数是将node和其直接前驱进行合并，也就是和它左边的neighbor_node进行合并；
 * @param neighbor_node sibling page of input "node" (neighbor_node是node的前结点)
 * @param node input from method coalesceOrRedistribute() (node结点是需要被删除的)
 * @param parent parent page of input "node"
 * @param index node在parent中的rid_idx
 * @return true means parent node should be deleted, false means no deletion happend
 */
bool IxIndexHandle::coalesce(IxNodeHandle **neighbor_node, IxNodeHandle **node, IxNodeHandle **parent, int index,
                             Transaction *transaction, bool *root_is_latched)
{
    if (index == 0)
    {
        // merge into the right sibling
        index = 1; // now node is the left sibling, and neighbor_node is the right sibling
        std::swap(*neighbor_node, *node);
    }
    if ((*node)->is_leaf_page())
        if (file_hdr_->last_leaf_ == (*node)->get_page_no())
            file_hdr_->last_leaf_ = (*neighbor_node)->get_page_no();
    int pos = (*neighbor_node)->get_size();
    (*neighbor_node)->insert_pairs(pos, (*node)->get_key(0), (*node)->get_rid(0), (*node)->get_size());
    for (int i = 0; i < (*node)->get_size(); ++i)
        maintain_child(*neighbor_node, pos + i);
    if ((*node)->is_leaf_page())
        erase_leaf(*node);
    release_node_handle(**node);
    (*parent)->erase_pair(index - 1);
    return coalesce_or_redistribute(*parent, transaction, root_is_latched);
}

/**
 * @brief 这里把iid转换成了rid，即iid的slot_no作为node的rid_idx(key_idx)
 * @param iid
 * @return Rid
 */
Rid IxIndexHandle::get_rid(const Iid &iid) const
{
    auto node = fetch_node(iid.page_no);
    if (iid.slot_no >= node->get_size())
    {
        delete node;
        throw IndexEntryNotFoundError();
    }
    Rid rid = *node->get_rid(iid.slot_no);
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);
    delete node;
    return rid;
}

/**
 * 获取iid所在节点的所有rids
 */
std::vector<Rid> IxIndexHandle::get_rids(const Iid &iid) const
{
    auto node = fetch_node(iid.page_no);
    if (iid.slot_no >= node->get_size() || iid.slot_no < 0)
    {
        delete node;
        throw IndexEntryNotFoundError();
    }
    std::vector<Rid> rids;
    for(int i = 0; i < node->get_size(); ++i)
    {
        rids.push_back(*node->get_rid(i));
    }
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);
    delete node;
    return rids;
}

/**
 * @brief FindLeafPage + lower_bound
 *
 * @param key
 * @return Iid
 */
Iid IxIndexHandle::lower_bound(const char *key)
{
    std::scoped_lock lock(root_latch_);
    auto [leaf_node, root_is_latched] = find_leaf_page(key, Operation::FIND, nullptr);
    if (leaf_node == nullptr) {
        return {IX_NO_PAGE, 0};
    }
    int index = leaf_node->lower_bound(key);
    Iid iid;
    if (index == leaf_node->get_size())
    {
        if (leaf_node->get_next_leaf() == IX_LEAF_HEADER_PAGE)
            iid = leaf_end();
        else
            iid = {leaf_node->get_next_leaf(), 0};
    }
    else
        iid = {leaf_node->get_page_no(), index};
    buffer_pool_manager_->unpin_page(leaf_node->get_page_id(), false);
    delete leaf_node;
    return iid;
}

/**
 * @brief FindLeafPage + upper_bound
 *
 * @param key
 * @return Iid
 */
Iid IxIndexHandle::upper_bound(const char *key)
{
    std::scoped_lock lock(root_latch_);
    auto [leaf_node, root_is_latched] = find_leaf_page(key, Operation::FIND, nullptr);
    if (leaf_node == nullptr) {
        return {IX_NO_PAGE, 0};
    }
    int index = leaf_node->upper_bound(key);
    Iid iid;
    if (index == leaf_node->get_size())
    {
        if (leaf_node->get_next_leaf() == IX_LEAF_HEADER_PAGE||leaf_node->get_next_leaf()==INVALID_PAGE_ID||leaf_node->get_page_no()==file_hdr_->last_leaf_)
            iid = leaf_end();
        else
            iid = {leaf_node->get_next_leaf(), 0};
    }
    else
        iid = {leaf_node->get_page_no(), index};
    buffer_pool_manager_->unpin_page(leaf_node->get_page_id(), false);
    delete leaf_node;
    return iid;
}

/**
 * @brief 指向最后一个叶子的最后一个结点的后一个
 * @return Iid
 */
Iid IxIndexHandle::leaf_end() const
{
    if (file_hdr_->root_page_ == IX_NO_PAGE ||
        file_hdr_->last_leaf_ == IX_NO_PAGE) {
        return {IX_NO_PAGE, 0};
    }
    auto node = fetch_node(file_hdr_->last_leaf_);
    Iid iid = {.page_no = file_hdr_->last_leaf_, .slot_no = node->get_size()};
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);
    delete node;
    return iid;
}

/**
 * @brief 指向第一个叶子的第一个结点
 * @return Iid
 */
Iid IxIndexHandle::leaf_begin() const
{
    if (file_hdr_->root_page_ == IX_NO_PAGE ||
        file_hdr_->first_leaf_ == IX_NO_PAGE) {
        return {IX_NO_PAGE, 0};
    }
    Iid iid = {.page_no = file_hdr_->first_leaf_, .slot_no = 0};
    return iid;
}

/**
 * @brief 获取一个指定结点
 *
 * @param page_no
 * @return IxNodeHandle*
 */
IxNodeHandle *IxIndexHandle::fetch_node(int page_no) const
{
    Page *page = buffer_pool_manager_->fetch_page(PageId{fd_, page_no});
    IxNodeHandle *node = new IxNodeHandle(file_hdr_, page);

    return node;
}

/**
 * @brief 创建一个新结点
 *
 * @return IxNodeHandle*
 */
IxNodeHandle *IxIndexHandle::create_node()
{
    IxNodeHandle *node;
    file_hdr_->num_pages_++;

    PageId new_page_id = {.fd = fd_, .page_no = INVALID_PAGE_ID};
    Page *page = buffer_pool_manager_->new_page(&new_page_id);
    node = new IxNodeHandle(file_hdr_, page);
    return node;
}

/**
 * @brief 从node开始更新其父节点的第一个key，一直向上更新直到根节点
 *
 * @param node
 */
void IxIndexHandle::maintain_parent(IxNodeHandle *node)
{
    IxNodeHandle *curr = node;
    while (curr->get_parent_page_no() != IX_NO_PAGE)
    {
        auto parent = fetch_node(curr->get_parent_page_no());
        int rank = parent->find_child(curr);
        if (rank == 0) {
            buffer_pool_manager_->unpin_page(parent->get_page_id(), false);
            if (curr != node) delete curr;
            curr = parent;
            continue;
        }
        char *parent_key = parent->get_key(rank - 1);
        char *child_first_key = curr->get_key(0);
        if (memcmp(parent_key, child_first_key, file_hdr_->col_tot_len_) == 0)
        {
            buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
            delete parent;
            break;
        }
        memcpy(parent_key, child_first_key, file_hdr_->col_tot_len_);
        buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        if (curr != node) delete curr;
        curr = parent;
    }
    if (curr != node) delete curr;
}

/**
 * @brief 要删除leaf之前调用此函数，更新leaf前驱结点的next指针和后继结点的prev指针
 *
 * @param leaf 要删除的leaf
 */
void IxIndexHandle::erase_leaf(IxNodeHandle *leaf)
{
    assert(leaf->is_leaf_page());

    if (leaf->get_prev_leaf() != IX_NO_PAGE) {
        auto prev = fetch_node(leaf->get_prev_leaf());
        prev->set_next_leaf(leaf->get_next_leaf());
        buffer_pool_manager_->unpin_page(prev->get_page_id(), true);
        delete prev;
    } else {
        file_hdr_->first_leaf_ = leaf->get_next_leaf();
    }

    if (leaf->get_next_leaf() != IX_NO_PAGE) {
        auto next = fetch_node(leaf->get_next_leaf());
        next->set_prev_leaf(leaf->get_prev_leaf());
        buffer_pool_manager_->unpin_page(next->get_page_id(), true);
        delete next;
    } else {
        file_hdr_->last_leaf_ = leaf->get_prev_leaf();
    }
}

/**
 * @brief 删除node时，更新file_hdr_.num_pages
 *
 * @param node
 */
void IxIndexHandle::release_node_handle(IxNodeHandle &node)
{
    // Page numbers are append-only because this implementation has no reusable
    // index-page free list. The caller owns the pin and will unpin the page.
    (void)node;
}

/**
 * @brief 将node的第child_idx个孩子结点的父节点置为node
 */
void IxIndexHandle::maintain_child(IxNodeHandle *node, int child_idx)
{
    if (!node->is_leaf_page())
    {
        int child_page_no = node->value_at(child_idx);
        auto child = fetch_node(child_page_no);
        child->set_parent_page_no(node->get_page_no());
        buffer_pool_manager_->unpin_page(child->get_page_id(), true);
        delete child;
    }
}

/**
 * @brief 重新分配内部节点的键值对
 * @param neighbor_node 兄弟节点
 * @param node 当前节点
 * @param parent 父节点
 * @param index node在parent中的位置
 */
void IxIndexHandle::redistribute_internal(IxNodeHandle *neighbor_node, IxNodeHandle *node, IxNodeHandle *parent, int index)
{
    if (index == 0) { // node is left, neighbor_node is right. move from neighbor to node
        // 1. copy parent key to node
        node->set_key(node->get_size(), parent->get_key(0));
        // 2. copy neighbor's first child to node
        node->set_rid(node->get_size() + 1, *neighbor_node->get_rid(0));
        node->set_size(node->get_size() + 1);
        // 3. update child's parent
        maintain_child(node, node->get_size());
        // 4. move neighbor's first key to parent
        parent->set_key(0, neighbor_node->get_key(0));
        
        // 5. remove key and child from neighbor manually
        for (int i = 0; i < neighbor_node->get_size() - 1; ++i) {
            neighbor_node->set_key(i, neighbor_node->get_key(i + 1));
        }
        for (int i = 0; i < neighbor_node->get_size(); ++i) {
            neighbor_node->set_rid(i, *neighbor_node->get_rid(i + 1));
        }
        neighbor_node->set_size(neighbor_node->get_size() - 1);

    } else { // node is right, neighbor_node is left. move from neighbor to node
        // Manually prepend the key and pointer.
        // 1. Shift everything in node to the right by one.
        for (int i = node->get_size(); i > 0; --i) {
            node->set_key(i, node->get_key(i - 1));
        }
        for (int i = node->get_size(); i >= 0; --i) {
            node->set_rid(i + 1, *node->get_rid(i));
        }

        // 2. Insert the new key from parent and pointer from neighbor.
        node->set_key(0, parent->get_key(index - 1));
        node->set_rid(0, *neighbor_node->get_rid(neighbor_node->get_size()));
        node->set_size(node->get_size() + 1);
        
        // 3. Update child's parent.
        maintain_child(node, 0);

        // 4. Update parent's key.
        parent->set_key(index - 1, neighbor_node->get_key(neighbor_node->get_size() - 1));

        // 5. Remove from neighbor.
        neighbor_node->erase_pair(neighbor_node->get_size() - 1);
    }
}

/**
 * @brief 合并内部节点
 * @param neighbor_node_ptr 左兄弟节点指针
 * @param node_ptr 右节点指针（将被删除）
 * @param parent_ptr 父节点指针
 * @param index node在parent中的位置
 * @return 是否需要继续向上合并
 */
bool IxIndexHandle::coalesce_internal(IxNodeHandle **neighbor_node_ptr, IxNodeHandle **node_ptr, IxNodeHandle **parent_ptr, int index,
                                     Transaction *transaction, bool *root_is_latched)
{
    auto neighbor_node = *neighbor_node_ptr;
    auto node = *node_ptr;
    auto parent = *parent_ptr;

    // The right node (node) is merged into the left node (neighbor_node).
    // The separator key is in the parent at index (index - 1).
    char* middle_key = parent->get_key(index - 1);
    int neighbor_original_size = neighbor_node->get_size();
    int node_size = node->get_size();

    // 1. Copy the separator key from the parent to the end of the neighbor node.
    neighbor_node->set_key(neighbor_original_size, middle_key);

    // 2. Copy all keys and pointers from the right node to the left node.
    memcpy(neighbor_node->get_key(neighbor_original_size + 1), node->get_key(0), node_size * file_hdr_->col_tot_len_);
    memcpy(neighbor_node->get_rid(neighbor_original_size + 1), node->get_rid(0), (node_size + 1) * sizeof(Rid));

    // 3. Update the key count of the merged node.
    neighbor_node->page_hdr->num_key = neighbor_original_size + 1 + node_size;

    // 4. Update the parent pointer of all children that were moved.
    for (int i = 0; i <= node_size; i++) {
        auto child = fetch_node(node->value_at(i));
        child->set_parent_page_no(neighbor_node->get_page_no());
        buffer_pool_manager_->unpin_page(child->get_page_id(), true);
        delete child;
    }

    // 5. Remove the key and pointer from the parent node.
    parent->erase_pair(index - 1);

    // 6. Release the page of the now-empty right node.
    release_node_handle(*node);

    // 7. The parent may now be under-full, so we recursively check it.
    return coalesce_or_redistribute(parent, transaction, root_is_latched);
}
