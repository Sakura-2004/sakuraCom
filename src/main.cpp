#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <iostream>
#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <thread>

#include "../inc/ui.hpp"

using namespace ftxui;

static volatile std::sig_atomic_t g_sigintReceived = 0;
static std::atomic<bool> g_workerRunning{true};

static void HandleSigint(int)
{
    g_sigintReceived = 1;
}

void t1Func(ftxui::App &screen,
            std::shared_ptr<serialSelectMenu> scM)
{
    while (g_workerRunning.load(std::memory_order_relaxed) &&
           g_sigintReceived == 0)
    {
        scM->RefreshSerial();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        screen.PostEvent(Event::Custom);
    }

    /* 唤醒UI线程，便于主线程退出事件循环并join。 */
    screen.PostEvent(Event::Custom);
}

int main()
{
    /* 准备其他信息 */

    /* 绘制ui*/
    auto screen = ScreenInteractive::Fullscreen(); // 终端空间大小
    //char buffer[128];

    auto scM = std::make_shared<serialSelectMenu>();

    auto outputL = Make<outputLable>();

    auto inputL = Make<inputLable>();
    inputL->setSendCallback(
        [scM](const std::string &data, bool)
        {
            return scM->tryWriteOnce(data);
        });

    auto root = Container::Vertical({scM,
                                     inputL,
                                     outputL});

    /* 整个布局 */
    auto component = Renderer(root, [&]
                              { Dimensions terminal_size = Terminal::Size();
                                int height=terminal_size.dimy-22;
                                inputL->setDataHeight(static_cast<int>(height*0.5));
                                outputL->setDataHeight(static_cast<int>(height*0.5));

                                return vbox({
                                           text("SakuraCom") | center,

                                           separator(),

                                           root->Render(),
                                           separator(),

                                           text("SakuraCom 1.1") | color(Color::GrayDark) | center,
                                       }) |
                                       border; });

    component = CatchEvent(component, [&](Event event)
                           {
        if (g_sigintReceived != 0 ||
            event == Event::Character('\x03')){
            g_workerRunning.store(false, std::memory_order_relaxed);
            screen.Exit();
            return true;
        }

        if(event == Event::Custom){
            /* 更新获取数据逻辑 100ms一次 */
            std::string str=scM->tryReadOnce();
            if(str.size()!=0){
                outputL->putString(str);
            }
            return true;
        }
        return false; });

    /* 准备多线程 */
    std::signal(SIGINT, HandleSigint);
    std::thread t1(t1Func, std::ref(screen), scM);

    /* 进入ui */
    screen.Loop(component);

    g_workerRunning.store(false, std::memory_order_relaxed);
    t1.join();
}
