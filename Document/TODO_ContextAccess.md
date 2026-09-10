# 取上下文就是声明访问权限

> **这份改动半途而废比现状更差。** 今天「加了组件必发事件」是无条件的；改到一半就变成
> 「看你走哪个门」，而且失败是静默的——组件加进去了，没人收到通知，没有任何提示。
> 先读完「唯一入口」那节再决定要不要开始。

## 命题

**拿到一个上下文，就等于声明了你要对它做什么。** 没声明的组件看不见，没声明的能力用不了。

今天不是这样：`ExecuteContext::Current()` 交出一个可写的裸上下文，拿到它就能看见和修改全部组件。
系统只有几个的时候这是便利；系统一多，「谁改了这个组件」就没有答案了。

---

## 现状：那个保证是实现细节给的

今天「`Add<T>` 一定会发事件」成立，只是因为 `WorldContext::Add` 是唯一的门——`BasicContext<Entity>`
特化把派发硬编码进了每个写操作的 `enable_if` 重载里。

**这个保证不是设计给的，是「碰巧只有一条路」给的。** 一旦派发挪走，`BasicContext::Add` 就是公开且
静默的。

绕过引用层的规模（不含测试）：

| 上下文 | `Current()` 调用点 | 文件 |
|---|---|---|
| `WorldExecuteContext` | 56 | 20 |
| `RHIExecuteContext` | 99 | 34 |
| `MaterialExecuteContext` | 12 | 7 |

而走了 `CurrentReference<Traits>()` 的只有 4 个文件（`MeshSystem` / `SkyboxSystem` /
`TransformSystem` / `IconManager`）。`SceneManager` 自己就有 23 处裸上下文。

三个上下文的性质不同，迁移的理由也不同：

- **`WorldExecuteContext`**：事件在这里，`BasicContext<Entity>` 是唯一带派发的特化。
- **`RHIExecuteContext` / `MaterialExecuteContext`**：用的是 `BasicContext` 主模板，**本来就没有事件**。
  这两个的迁移纯粹是权限轴的事，和事件无关。

---

## 三个轴，它们不是一类东西

| 轴 | 它改变什么 | 归属 |
|---|---|---|
| **权限** | 编译期裁剪。留下的部分语义不变 | `Traits` 上的标志 |
| **事件** | 附加副作用。返回值和可见性不变 | `Traits` 上的标志 |
| **延迟提交** | **写入何时可见**。`Add` 之后紧跟 `Get` 拿不到 | **另一个类型，或另一组方法名** |

前两个可以并列在同一份 `Traits` 上。第三个不行：它不是副作用，是语义。放进同一个 `Add` 里，那个方法
就变成「有时是加了，有时是将要加」，而调用点看不出区别——多线程下最难查的一类 bug。

**让「延迟」在调用点可见**，例如 `ctx.Commands().Add<T>(...)`。

（命令列表什么时候 flush、和调度器怎么配合，不属于这份文档。这里只管它在类型上怎么表达。）

---

## 类型结构

```
ContextStorage<E>                 数据存储 + 基础操作，不认识事件、权限、系统
├── BasicContext<E>               活上下文，能进 ExecuteContext 栈
└── StagingContext<E>             暂存，只能是 Merge 的源

ContextReference<Ctx, Traits>     唯一的变更入口
```

**`StagingContext` 不能继承 `BasicContext`。** 一旦 is-a 成立，它就能绑到
`Merge(MergeContextT<E>& target, ...)` 的 target 参数上，自合并的编译期保证当场失效。两者必须是
兄弟：工具吃基类 `ContextStorage<E>&`，`Merge` 的两个参数各吃各自的派生类型。

### `BasicContext<Entity>` 特化会整个消失

那个特化里除了事件机械（成对的 `enable_if` 写操作重载 + `m_entityRemoveEvents` 那个运行期 TypeId
集合）**没有一样东西是主模板没有的**。派发搬走之后它是空的：

```cpp
using WorldContext = BasicContext<Entity>;   // 不再需要特化
```

和 `using MaterialContext = BasicContext<MaterialHandle>;` 完全同构——材质上下文今天用的就是主模板，
等于已经在目标形态上了。`ContextTraits` 那层「特化决定上下文类型」的转发也跟着可以删。

---

## 唯一入口

这节是全文的重心。**验收标准必须是编译期性质**，否则「唯一入口」只是愿望：

1. **`ContextStorage` / `BasicContext` 的写操作收成非公开**，`ContextReference` 做 friend。读操作
   可以继续公开——看见不是问题，改动才是。
2. **`ExecuteContext::Current()` 不再交出可写指针**。要写就得走 `CurrentReference<Traits>()`。
3. **`ContextReference::GetContext()` 那个后门收掉**（`ContextReference.h:223`），否则前两条白做。

做到这三条，「加组件必发事件」重新变成保证——而且这次是设计给的，不是碰巧只有一条路。

---

## 迁移

主体是机械替换：`Current()` → `CurrentReference<Traits>()`，然后每个调用点声明自己要什么。量大但
形状单一。

建议顺序（每步都能独立验）：

| | 内容 | 为什么先它 |
|---|---|---|
| 1 | 抽 `ContextStorage` 基类 | 纯去重，行为零变化。三个类今天各自重复同一套数据面 |
| 2 | 事件派发从 `BasicContext<Entity>` 搬到 `ContextReference`，特化删除 | 之后 `Add` 变静默，但入口还没收窄——**这一步之后到第 4 步之间是危险区** |
| 3 | `WorldExecuteContext` 的 56 处迁到 `CurrentReference` | 危险区最短化，先迁有事件的那个上下文 |
| 4 | 收窄三个后门（写操作私有 / `Current()` / `GetContext()`） | 保证在这一步成立 |
| 5 | `RHI` / `Material` 那 111 处 | 纯权限轴，没有正确性压力，可以慢慢来 |

第 2 步到第 4 步之间事件是「看门」状态，**这段区间越短越好**，不要中途插入别的工作。

### 一个真实的坎：反射驱动的调用方

编辑器的 `Inspector` / `ComponentView` / `MenuBar` 靠反射检视**任意**组件，没有静态类型清单，声明不了
具体的 acquires。

逃生口已经在了：`ReadWriteComponent<All>`（`ContextReference.h` 的 `AcquireMatches` 认这个）。用在
这里是诚实的——它们真的需要全部。第一个撞上的人会以为设计崩了，所以写在这里。

---

## 不属于这份文档的

- 命令列表的 flush 时机、和调度器的配合——执行模型的问题。
- `IScene` 的接口形态、层级操作往哪一层放——见 `TODO_ContextMerge.md` 的「前置条件」。
- 具体的 `Traits` 语法。等第 2 步真做时再定。
