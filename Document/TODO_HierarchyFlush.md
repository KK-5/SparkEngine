# 层级改动是指令，接入只发生在一处

> **半途而废比现状更差。** 只要还有一个地方在立即接入、另一个地方在延迟接入，同一个实体就会被接进
> 链表两次。四个接入点必须一起改，不能分批。

## 命题

**改动 `Hierarchy` 只是记下一条指令；把它接进链表这件事，只发生在 flush 一处。**

---

## 病根

今天接入（`AddEntityInternal`）有 **4 个调用点**，摘出（`RemoveEntityInternal`）有 **3 个**：
`SetParent` 两个重载、`OnComponentConstruct`、`OnComponentsConstruct`、`OnComponentUpdated`、
`OnComponentWillUpdate`、`OnComponentDestory`。改一处要同时想到七处。

`Valid` 还混着两个不同的问题：

- **这份描述指向的东西存在吗**——真正的校验。
- **它还没被接进去吧**——三处（`SceneManager.cpp:406` / `:422` / `:444`）假设了这一点。

第二类假设让「一批已经自洽的组件」必然校验失败，所以 merge 那条路只能靠 `Has<MergedFrom>` 绕开它。

---

## 机制

### 1. 指令

```cpp
struct PendingHierarchy
{
    enum class Kind { Add, Update, Remove };

    Kind      kind;
    Hierarchy previous;   //!< Update / Remove 摘旧位置要用；Add 忽略
};
```

**是带载荷的指令，不是 tag。** `Update` / `Remove` 执行时旧的 `Hierarchy` 已经被覆盖或擦掉了，
不自带就没了。

**一个实体同时只能带一条。** 合并规则：

```
已有指令 → 只改 kind，previous 不动
无指令   → 记 (kind, previous)
```

`previous` 取**最早**的那份、`kind` 取**最新**的那份。反过来会错：`Update`（旧值 = 已接入的 A）之后
紧跟 `Remove`（旧值 = 刚写进去还没接入的 B），若整条覆盖，flush 会拿从未接入过的 B 去摘邻居。

带 `Remove` 指令的实体**不允许被别的 `Hierarchy` 引用**。

### 2. flush 是幂等的

对一条指令做的事是：**让邻居和这个实体自己说的保持一致**。已经一致就什么都不做。

于是「这是一份还没接进去的请求，还是一份已经接好的事实」**这个问题不用问**——merge 搬来的那批已经
一致，flush 全是空操作；新写的请求不一致，接进去。`SceneManager` 里那个 `Has<MergedFrom>` 的耦合
随之删除。

### 3. `prevSibling == NullEntity` 定义为「排第一」

含糊性按定义消失：「父节点已经有孩子而你没说插哪」不再是错误，而是明确的「插最前」。

`Valid` 因此塌成两条：引用的实体存在吗、`prevSibling` 是不是同一个父节点的孩子。那三处「尚未接入」
的假设整段删掉。

---

## 两条必须写死的契约

**① flush 按打标记的顺序执行。** entt 的 storage 是**反向迭代**的，不能让它替你决定顺序——指令自带
序号，或者用别的容器。

这条有可见后果：今天两次 `SetParent(A, P)` / `SetParent(B, P)` 立即执行，各自头插，结果是 **B 在前**
（调用顺序的反序）。改成按打标记顺序 flush 之后，结果是 **A 在前**。`SpawnModel` 的图元排列直接受
影响。**这是主动选择，不是副作用。**

**② 世界侧的 flush 必须在返回调用方之前完成。** 每个事件之后、或每个批量事件之后。

mark 到 flush 之间树是**不自洽**的——邻居还指着一个已经没有 `Hierarchy` 的实体，此时遍历会看到坏树。
暂存侧没有这个问题（没人在遍历），所以它可以自己攒完再手动 flush。谁要是把世界侧的 flush 挪到帧末，
这条就是它炸掉的原因。

---

## 收益

- 接入点 4 → 1。
- `Valid` 从五条塌成两条，删掉「尚未接入」那类假设。
- `SceneManager` 不再需要知道 merge 存在。
- `SetParent` 退化成便利函数（填两个字段 + 打指令），不再是第二条通路。**「直接 `Add<Hierarchy>`」和
  「`SetParent`」两种入口合一**，这是这次重新设计的起点。

摘出仍然是 2-3 个点，对称只有一半：`WillUpdate` / `Destory` 必须在旧数据消失之前把 `previous` 记下来。
这不是缺陷，是时序决定的。

---

## 撤回的一个想法

曾考虑把 `Hierarchy` 的字段分成「作者写的 `parent` / `prevSibling`」和「flush 算的 `firstChild` /
`nextSibling`」。**不做**，两条理由：

- 没有任何手段限制作者不写派生字段，约定强制不了。
- flush 自己也要**读**那些字段——它把实体接进父节点链表时要读 `parent.firstChild`，是增量维护不是
  全局重算。所以派生字段是 flush 依赖的状态，不是它单方面拥有的输出。

---

## 影响面

改写：`Valid`、`AddEntityInternal`、`RemoveEntityInternal`、`SetParent` ×2、四个事件 handler。
新增：`PendingHierarchy` 和 flush。

护栏只有 `SceneManagerTest` 8 个用例 + `MergeSceneTest` 6 个。**动手前先补测试**，尤其是兄弟顺序和
「摘旧位置」这两块——它们是最容易静默错的。

## 不属于这份文档的

- `IScene` 的接口形态（查询接口换 visitor 那批）。
- 指令能不能跨帧攒着——不能，见契约②。要跨帧的是另一个机制。
