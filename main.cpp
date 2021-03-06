#include <windows.h>
#include <iostream>
#include "string"
#include "ctime"

using namespace std;

#define NUMBER_OF_CONSUMERS 10
#define NUMBER_OF_PRODUCERS 8
#define MAX_QUEUE_CAPACITY 6
#define PRODUCER_WORK_TIME 250
#define CONSUMER_WORK_TIME 100
#define TIME_OF_WORK 3000

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
    string info;

    Data(int producerID) {
        int value = rand() % 10000;
        info = "|Product : " + to_string(value) + " ID: " + to_string(producerID) + "|";
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

void myPrint(string message) {
    outputSection.enter();// блокуємо доступ до консолі для інших потоків
    cout << message << endl;
    outputSection.leave();// повертаємо доступ до консолі для інших потоків
}

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
            Sleep(PRODUCER_WORK_TIME);
            producerSection.enter();//блокуємо інших виробників, бо тільки один може класти в чергу
            put();
            producerSection.leave();//даємо доступ іншим виробникам
        }
    }

    void put() {
        counterSection.enter();// блокуємо доступ до змінної counter для інших потоків
        if (counter == MAX_QUEUE_CAPACITY) {
            counterSection.leave();// повертаємо доступ до змінної counter для інших потоків
            myPrint("Queue is full. Producer " + to_string(id) + " is waiting for consumers");
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
            //можна повертати доступ до змінної, тому що споживач з виробником модифікують різні кінці черги, бо елемент не один у черзі
            queue.push(product);
        }
        printDeliveredProduct(product);
        product = NULL;//товар успішно доставлено і можна створювати новий
    }

    void printDeliveredProduct(Data *product) {
        string message = product->info + " is delivered in queue";
        myPrint(message);
    }

    ~Producer() {
        if (product != NULL) {
            delete product;
        }
    }
};

struct Consumer {
    int id;
    string strID;

    Consumer(int id) {
        this->id = id;
        strID = to_string(id);
    }

    void work() {
        while (isWork) {
            Sleep(CONSUMER_WORK_TIME);
            consumerSection.enter();//блокуємо інших споживачів, бо тільки один може брати з черги
            Data *product = get();
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
            myPrint("Queue is empty. Consumer " + to_string(id) + " is waiting for producers");
            counterSection.leave();
            myPrint("before cs in cons " + to_string(id) + " method get, case counter == 0");
            noProductsSection.enter();//чекаємо поки якийсь виробник не покладе у чергу товар
            myPrint("after cs in cons " + to_string(id) + " method get, case counter == 0");
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
            //можна повертати доступ до змінної, тому що споживач з виробником модифікують різні кінці черги, бо елемент не один у черзі
            product = queue.pop();
        }
        printConsumedProduct(product);
        return product;
    }

    void printConsumedProduct(Data *product) {
        string message = product->info + " is consumed by Consumer: " + to_string(id);
        myPrint(message);
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

    DWORD waitAll() {
        return WaitForMultipleObjects(NUMBER_OF_PRODUCERS,
                                      producerThread,
                                      TRUE,
                                      PRODUCER_WORK_TIME);
    }

    ~ProducerWorkPlace() {
        while (waitAll() != WAIT_OBJECT_0) {
            fullQueueSection.leave(); //на випадок коли всі споживачі вже сплять, а виробники чекають на звільнення черги
        }
        for (auto &i: producerThread) {
            CloseHandle(i);//закриття дескриптора потоку
        }
        myPrint("Producers have finished work");
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

    DWORD waitAll() {
        return WaitForMultipleObjects(NUMBER_OF_CONSUMERS,
                                      consumerThread,
                                      TRUE,
                                      CONSUMER_WORK_TIME);
    }

    ~ConsumerWorkPlace() {
        while (waitAll() != WAIT_OBJECT_0) {
            noProductsSection.leave();//на випадок коли всі виробники вже сплять, а споживачі чекають через пусту чергу
        }
        for (auto &i: consumerThread) {
            CloseHandle(i);//закриття дескриптора потоку
        }
        myPrint("Consumers have finished work");
    }
};

void saySize() {
    outputSection.enter();
    cout << "size == " << counter << endl;
    outputSection.leave();
}

int main() {
    ConsumerWorkPlace consumers;
    ProducerWorkPlace producers;
    noProductsSection.enter();
    producers.start();
    consumers.start();
    Sleep(TIME_OF_WORK);
    isWork = false;
    return 0;
}
