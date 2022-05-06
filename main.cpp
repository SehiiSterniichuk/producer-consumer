#include <windows.h>
#include <iostream>
#include "string"
#include "ctime"


using namespace std;

#define NUMBER_OF_CONSUMERS 10
#define NUMBER_OF_PRODUCERS 7
#define MAX_QUEUE_CAPACITY 5
#define MAKE_PRODUCT_TIME 250
#define CONSUME_PRODUCT_TIME 250
#define TIME_OF_WORK 2000

struct CriticalSection {//структура "обгортка" для критичних секцій, створена просто для зручності
    CRITICAL_SECTION section;

    CriticalSection() {
        InitializeCriticalSection(&section);
    }

    void enter() {
        EnterCriticalSection(&section);
    }

    void leave() {
        LeaveCriticalSection(&section);
    }

    ~CriticalSection() {
        DeleteCriticalSection(&section);
    }
};

CriticalSection producerSection;// гарантує, що у чергу буде додавати лише один виробник
CriticalSection consumerSection;// гарантує, що з черги буде брати лише один споживач
CriticalSection noProductsSection;//випадок коли черга пуста
CriticalSection fullQueueSection;//випадок коли черга повна
CriticalSection counterSection;//критична секція, яка гарантує, що до змінної, котра зберігає розмір черги, буде мати доступ лише один потік
CriticalSection outputSection;//гарантує, що до консолі матиме доступ лише один потік
struct Data {
    int value;
    string info;

    Data(int idOfProducer) {
        value = rand() % 10000;
        info = "Product is made by producer " + to_string(idOfProducer) + ", value: " + to_string(value) + ".";
    }
};

struct Node {
    Node *prev;
    Node *next;
    Data *data;

    Node(Node *prev, Node *next, Data *data) {
        this->prev = prev;
        this->data = data;
        this->next = next;
    }
};


int counter = 0;

struct Queue {
    Node *head = NULL;
    Node *tail = NULL;

    void push(Data *data) {
        if (head == NULL) {
            head = new Node(NULL, NULL, data);
            tail = head;
            return;
        }
        Node *newbie = new Node(tail, NULL, data);
        tail->next = newbie;
        newbie->prev = tail;
        tail = newbie;
    }

    Data *pop() {
        if (tail == NULL) {
            return NULL;
        }
        Data *data = tail->data;
        Node *oldNode = tail;
        tail = tail->prev;
        if (NULL != tail) {
            tail->next = NULL;
        } else {
            head = NULL;
        }
        delete oldNode;
        return data;
    }

    ~Queue() {
        while (head != NULL) {
            Data *data = pop();
            delete data;
        }
    }
};

Queue queue;
bool isWork = true;// коли true - споживачі та виробники працюють і false - коли треба завершувати програму

struct Producer {
    int id;
    Data *product = NULL;

    Producer(int id) {
        this->id = id;
    }

    void work() {
        srand(id);
        while (isWork) {
            //якщо product != NULL, то це означає, що в попередній раз черга була повна і виробник не повинен знову створювати товар
            if (product == NULL) {
                product = new Data(id);
            }
            Sleep(MAKE_PRODUCT_TIME);
            producerSection.enter();//блокуємо інших виробників, бо тільки один може класти в чергу
            put();
            producerSection.leave();//даємо доступ іншим виробникам
        }
    }

    void put() {
        counterSection.enter();// блокуємо доступ до змінної counter для інших потоків
        if (counter == MAX_QUEUE_CAPACITY) {
            counterSection.leave();// повертаємо доступ до змінної counter для інших потоків
            outputSection.enter();// блокуємо доступ до консолі для інших потоків
            cout << "Queue is full. Producer " << id << " is waiting for consumers" << endl;
            outputSection.leave();// повертаємо доступ до консолі для інших потоків
            fullQueueSection.enter();//чекаємо поки який-небудь споживач не візьме товар з черги і поверне доступ
            return;
        } else if (counter == 0) {
            queue.push(product);
            counter++;
            counterSection.leave();
            noProductsSection.leave();//виходимо з критичної секції у яку ввійшов споживач, що виявив у черзі 0 товарів
        } else if (counter == 1) {
            queue.push(product);
            counter++;
            counterSection.leave();
        } else {
            counter++;
            counterSection.leave();
            queue.push(product);
        }
        printDeliveredProduct(product);
        product = NULL;//товар успішно доставлено і можна створювати новий
    }

    void printDeliveredProduct(Data *product) {
        outputSection.enter();
        cout << product->info << " Product is delivered in queue" << endl;
        outputSection.leave();
    }

    ~Producer() {
        if (product != NULL) {
            delete product;
        }
    }
};

struct Consumer {
    int id;

    Consumer(int id) {
        this->id = id;
    }

    void work() {
        while (isWork) {
            consumerSection.enter();//блокуємо інших споживачів, бо тільки один може брати з черги
            Data *product = get();
            Sleep(CONSUME_PRODUCT_TIME);
            consumerSection.leave();//повертаємо доступ до черги для інших споживачів
            if (product != NULL) {
                delete product;
            }
        }
    }

    Data *get() {
        counterSection.enter();//блокуємо доступ до змінної counter для інших потоків
        Data *product = NULL;
        if (counter == 0) {
            outputSection.enter();
            cout << "Queue is empty. Consumer " << id << " is waiting for producers" << endl;
            outputSection.leave();
            counterSection.leave();
            noProductsSection.enter();//чекаємо поки якийсь виробник не покладе у чергу товар
            return NULL;
        } else if (counter == MAX_QUEUE_CAPACITY) {
            product = queue.pop();
            counter--;
            counterSection.leave();
            fullQueueSection.leave();// "будимо" виробника, який очікував тому що черга була повна
        } else if (counter == 1) {
            product = queue.pop();
            counter--;
            counterSection.leave();
        } else {
            counter--;
            counterSection.leave();
            product = queue.pop();
        }
        printConsumedProduct(product);
        return product;
    }

    void printConsumedProduct(Data *product) {
        outputSection.enter();
        cout << product->info << " Product is consumed by Consumer: " << id << endl;
        outputSection.leave();
    }
};


struct ProducerWorkPlace {
    HANDLE producerThread[NUMBER_OF_PRODUCERS];

    void start() {
        //створюємо потоки виробників
        for (int i = 0; i < NUMBER_OF_PRODUCERS; ++i) {
            Producer *producer = new Producer(i);
            producerThread[i] = CreateThread(0,
                                             0,
                                             startProducer,
                                             producer,
                                             0,
                                             NULL);
        }

    }

    static DWORD WINAPI startProducer(LPVOID lpParameter) {//функція з якої починається робота виробника
        auto *producer = (Producer *) lpParameter;
        producer->work();
        delete producer;//видаляємо виробника після завершення роботи
        return 0;
    }

    ~ProducerWorkPlace() {
        WaitForMultipleObjects(NUMBER_OF_PRODUCERS,  //кількість дескрипторів
                               producerThread,   // масив дескрипторів кожного потоку
                               TRUE,/*Якщо цей параметр має значення TRUE,
 * функція повертається, коли сигналізується стан усіх об’єктів у масиві threads*/
                               INFINITE);//час очікування об'єктів
        for (auto &i: producerThread) {
            CloseHandle(i);//закриття дескриптора потоку
        }
        outputSection.enter();
        cout << "Producers have finished work" << endl;
        outputSection.leave();
    }
};

struct ConsumerWorkPlace {
    HANDLE consumerThread[NUMBER_OF_CONSUMERS];

    void start() {
        //створюємо потоки виробників
        for (int i = 0; i < NUMBER_OF_CONSUMERS; ++i) {
            Consumer *consumer = new Consumer(i);
            consumerThread[i] = CreateThread(0,
                                             0,
                                             startConsumer,
                                             consumer,
                                             0,
                                             NULL);
        }
    }

    static DWORD WINAPI startConsumer(LPVOID lpParameter) {//функція з якої починається робота споживача
        auto *consumer = (Consumer *) lpParameter;
        consumer->work();
        delete consumer;//видаляємо споживача після завершення його роботи
        return 0;
    }

    ~ConsumerWorkPlace() {
        WaitForMultipleObjects(NUMBER_OF_CONSUMERS,
                               consumerThread,
                               TRUE,
                               INFINITE);
        for (auto &i: consumerThread) {
            CloseHandle(i);//закриття дескриптора потоку
        }
        outputSection.enter();
        cout << "Consumers have finished work" << endl;
        outputSection.leave();
    }
};

int main(int argc, char *argv[]) {
    ConsumerWorkPlace consumers;
    ProducerWorkPlace producers;
    producers.start();
    noProductsSection.enter();
    consumers.start();
    Sleep(TIME_OF_WORK);
    isWork = false;
    return 0;
}