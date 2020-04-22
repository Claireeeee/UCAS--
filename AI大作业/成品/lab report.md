## 冬奥会问答系统实验报告

小组成员：余成，李嘉珊，曹琬璐

（所有工作均由3人商讨之后共同完成）

### 整体思路

利用open nmt翻译框架，将问答对的问题和答案分别作为翻译源信息和目标信息展开训练，生成字典和翻译模型，用翻译模型处理问句，翻译结果即作为答案输出

### 具体流程

#### 配置open nmt环境

##### 1. 安装Torch

```
git clone https://github.com/torch/distro.git ~/torch --recursive
cd ~/torch; bash install-deps;
./install.sh
source ~/.profile
```

##### 2. 安装依赖库

```
luarocks install tds
luarocks install bit32
```

#### 训练翻译模型

##### 1. 生成词典与训练数据

包括以下三个文件：

```
demo.src.dict
demo.tgt.dict
demo-train.t7
```

##### 2. 训练

共训练13个 epoch，生成13个模型

![image-20200110162708426](/Users/caowanlu/Library/Application Support/typora-user-images/image-20200110162708426.png)

#### 使用模型翻译

将需要翻译的问句放在test_ques.txt文件中，启动模型翻译，结果保存在同文件夹下pred.txt中

### 正确率测试

test文件夹下保存了3次测试的结果，

- test_ques.txt文件是用来测试的50个问句
- test_ans.txt文件是50个问题的标准答案
- pred.txt为模型翻译的结果

3次测试的平均正确率为14%



### 备注

本实验没有实现json格式的输入输出支持，如果想要测试模型，需要安装open nmt环境，使用如下命令调用我们的模型

```
th translate.lua -model demo-model_epoch13_48.65.t7 -src test_ques.txt -output pred.txt
```

其中test_ques.txt中放置需要查询的问句，翻译结果会保存在pred.txt中

