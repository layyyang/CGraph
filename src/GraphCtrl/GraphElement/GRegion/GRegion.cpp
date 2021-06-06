/***************************
@Author: Chunel
@Contact: chunel@foxmail.com
@File: GRegion.cpp
@Time: 2021/6/1 10:14 下午
@Desc: 
***************************/

#include "GRegion.h"

GRegion::GRegion() : GElement() {
    manager_ = new(std::nothrow) GElementManager();
    thread_pool_ = nullptr;
    is_init_ = false;
}


GRegion::~GRegion() {
    CGRAPH_DELETE_PTR(manager_)
}


GRegion::GRegion(const GRegion& region) {
    for (GElementPtr element : region.manager_->manager_elements_) {
        this->manager_->manager_elements_.insert(element);
    }
    this->thread_pool_ = region.thread_pool_;
}


GRegion& GRegion::operator=(const GRegion& region){
    if (this == &region) {
        return (*this);
    }

    for (GElementPtr element : region.manager_->manager_elements_) {
        this->manager_->manager_elements_.insert(element);
    }
    this->thread_pool_ = region.thread_pool_;

    return (*this);
}


CSTATUS GRegion::init() {
    CGRAPH_FUNCTION_BEGIN
    // 在这里将初始化所有的节点信息，并且实现分析，联通等功能
    CGRAPH_ASSERT_NOT_NULL(thread_pool_)
    CGRAPH_ASSERT_NOT_NULL(manager_)

    status = this->manager_->init();
    CGRAPH_FUNCTION_CHECK_STATUS

    status = analyse();
    CGRAPH_FUNCTION_CHECK_STATUS

    is_init_ = true;
    CGRAPH_FUNCTION_END
}


CSTATUS GRegion::deinit() {
    CGRAPH_FUNCTION_BEGIN
    status = manager_->deinit();

    CGRAPH_FUNCTION_END
}


CSTATUS GRegion::run() {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_INIT(true)
    CGRAPH_ASSERT_NOT_NULL(thread_pool_)
    CGRAPH_ASSERT_NOT_NULL(manager_)

    int runNodeSize = 0;
    std::vector<std::future<CSTATUS>> futures;

    for (GClusterArr& clusterArr : para_cluster_arrs_) {
        futures.clear();

        for (GCluster& cluster : clusterArr) {
            futures.push_back(std::move(this->thread_pool_->commit(cluster)));
            runNodeSize += cluster.getElementNum();
        }

        for (auto& fut : futures) {
            status = fut.get();
            CGRAPH_FUNCTION_CHECK_STATUS
        }
    }

    status = checkFinalStatus(runNodeSize);
    CGRAPH_FUNCTION_END
}


CSTATUS GRegion::process(bool isMock) {
    CGRAPH_FUNCTION_BEGIN
    status = this->beforeRun();
    CGRAPH_FUNCTION_CHECK_STATUS
    if (!isMock) {
        // 运行region中的信息。这里的信息，已经提前被解析了。
        status = run();
        CGRAPH_FUNCTION_CHECK_STATUS
    }

    status = this->afterRun();
    CGRAPH_FUNCTION_END
}


CSTATUS GRegion::beforeRun() {
    CGRAPH_FUNCTION_BEGIN

    for (GElementPtr element : this->region_elements_) {
        status = element->beforeRun();
        CGRAPH_FUNCTION_CHECK_STATUS
    }

    this->done_ = false;
    this->left_depend_ = this->dependence_.size();

    CGRAPH_FUNCTION_END
}


CSTATUS GRegion::afterRun() {
    CGRAPH_FUNCTION_BEGIN
    for (GElementPtr element : this->region_elements_) {
        status = element->afterRun();
        CGRAPH_FUNCTION_CHECK_STATUS
    }

    for (auto& element : this->run_before_) {
        element->left_depend_--;
    }
    this->done_ = true;
    CGRAPH_FUNCTION_END
}


CSTATUS GRegion::analyse() {
    CGRAPH_FUNCTION_BEGIN

    int runElementSize = 0;
    int totalElementSize = manager_->manager_elements_.size();

    GClusterArr curClusterArr;    // 记录每一层，可以并行的逻辑
    for (GElementPtr element : manager_->manager_elements_) {
        if (!element->isRunnable() || element->isLinkable()) {
            continue;
        }

        GCluster curCluster;
        GElementPtr curElement = element;
        curCluster.addElement(curElement);

        /* 将linkable的节点，统一放到一个cluster中 */
        while (1 == curElement->run_before_.size()
               && (*curElement->run_before_.begin())->isLinkable()) {
            // 将下一个放到cluster中处理
            curElement = (*curElement->run_before_.begin());
            curCluster.addElement(curElement);
        }
        curClusterArr.emplace_back(curCluster);
    }
    para_cluster_arrs_.emplace_back(curClusterArr);

    GClusterArr runnableClusterArr;
    while (!curClusterArr.empty() && runElementSize <= totalElementSize) {
        runnableClusterArr = curClusterArr;
        curClusterArr.clear();

        for (GCluster& cluster : runnableClusterArr) {
            status = cluster.process(true);    // 不执行run方法的process
            CGRAPH_FUNCTION_CHECK_STATUS
        }
        runElementSize += runnableClusterArr.size();

        std::set<GElementPtr> duplications;
        for (GCluster& cluster : runnableClusterArr) {
            for (GElementPtr element : cluster.cluster_elements_) {
                for (GElementPtr cur : element->run_before_) {
                    /**
                     * 判断element是否需要被加入
                     * 1，该元素是可以执行的
                     * 2，改元素本次循环是第一次被遍历
                     */
                    if (cur->isRunnable()
                        && duplications.find(cur) == duplications.end()) {
                        GCluster curCluster;
                        GElementPtr curElement = cur;
                        curCluster.addElement(curElement);
                        duplications.insert(curElement);

                        while (curElement->isLinkable()
                               && 1 == curElement->run_before_.size()
                               && (*curElement->run_before_.begin())->isLinkable()) {
                            curElement = (*curElement->run_before_.begin());
                            curCluster.addElement(curElement);
                            duplications.insert(curElement);
                        }
                        curClusterArr.emplace_back(curCluster);
                    }
                }
            }
        }

        /* 为空的话，直接退出循环；不为空的话，放入para信息中 */
        if (!curClusterArr.empty()) {
            para_cluster_arrs_.emplace_back(curClusterArr);
        }
    }

    CGRAPH_FUNCTION_END
}


CSTATUS GRegion::setThreadPool(GraphThreadPool* pool) {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_NOT_NULL(pool)

    this->thread_pool_ = pool;
    CGRAPH_FUNCTION_END
}


CSTATUS GRegion::addElement(GElementPtr element) {
    CGRAPH_FUNCTION_BEGIN
    CGRAPH_ASSERT_NOT_NULL(element)
    CGRAPH_ASSERT_INIT(false)

    this->region_elements_.emplace_back(element);
    CGRAPH_FUNCTION_END
}


int GRegion::getElementNum() {
    return this->region_elements_.size();
}


bool GRegion::isElementDone() {
    /* 所有内容均被执行过 */
    return std::all_of(region_elements_.begin(), region_elements_.end(),
                       [](const GElementPtr element) {
        return element->done_;
    });
}


CSTATUS GRegion::checkFinalStatus(int runNodeSize) {
    CGRAPH_FUNCTION_BEGIN

    status = (runNodeSize == manager_->manager_elements_.size()) ? STATUS_OK : STATUS_ERR;
    CGRAPH_FUNCTION_CHECK_STATUS

    /* 需要验证每个cluster里的每个内容是否被执行过一次 */
    for (GClusterArr& clusterArr : para_cluster_arrs_) {
        for (GCluster& cluster : clusterArr) {
            status = cluster.isElementsDone() ? STATUS_OK : STATUS_ERR;
            CGRAPH_FUNCTION_CHECK_STATUS
        }
    }

    CGRAPH_FUNCTION_END
}