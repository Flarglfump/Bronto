/* Tests: BrontoScript and the Script component (script.h). */
#include "test.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static br_world W;
static br_builtin_ids ID;
static br_component SCRIPT, RB, COL;
static br_engine g_engine;                 /* scripts need world->context; only input/mixer/time are used */
static char g_printed[512];

static void on_print(const char *text, void *user) {
    BR_UNUSED(user);
    snprintf(g_printed, sizeof(g_printed), "%s", text);
}

static void setup(void) {
    memset(&g_engine, 0, sizeof(g_engine));
    br_input_init(&g_engine.input);
    br_mixer_init(&g_engine.mixer, 44100);
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    W.context = &g_engine;
    SCRIPT = br_component_find(&W, "Script");
    RB = br_component_find(&W, "RigidBody");
    COL = br_component_find(&W, "Collider");
    br_assets_set_root(g_out_dir);
    br_script_set_print(on_print, NULL);
    g_printed[0] = 0;
    br_save_set_folder(test_path("saves"));      /* (saved data stays out of the real place) */
}

static void teardown(void) {
    br_world_destroy(&W);
    br_script_set_strict(false);
    br_assets_clear();
    br_assets_set_root("");
    br_script_set_print(NULL, NULL);
}

static void write_script(const char *name, const char *source) {
    CHECK(br_file_write_all(test_path(name), source, strlen(source)));
}

static br_entity scripted(const char *name, const char *file, const char *vars, br_vec2 pos) {
    br_entity e = br_entity_create(&W);
    br_script *s;
    snprintf(((br_name *)br_add(&W, e, ID.name, NULL))->text, 32, "%s", name);
    ((br_transform *)br_add(&W, e, ID.transform, NULL))->position = pos;
    s = (br_script *)br_add(&W, e, SCRIPT, NULL);
    snprintf(s->file, sizeof(s->file), "%s", file);
    snprintf(s->vars, sizeof(s->vars), "%s", vars ? vars : "");
    return e;
}

static void steps(int n) { while (n-- > 0) br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60.0f); }

static double num(br_entity e, const char *var) {
    double v = -12345;
    CHECK(br_script_get_number(&W, e, var, &v));
    return v;
}

static void test_check_and_errors(void) {
    char err[256];
    CHECK(br_script_check("var a = 1\nfunc update(dt) {\n  a += dt\n}\n", "ok.bs", err, sizeof(err)));
    CHECK(!br_script_check("func f() {\n  x = 1\n}\n", "a.bs", err, sizeof(err)));
    CHECK(strstr(err, "a.bs:2") && strstr(err, "unknown name 'x'"));
    CHECK(!br_script_check("func f() {\n  jmup()\n}\n", "b.bs", err, sizeof(err)) && strstr(err, "b.bs:2: unknown function 'jmup'"));
    CHECK(!br_script_check("func f() {\n  if true {\n", "c.bs", err, sizeof(err)) && strstr(err, "never closed"));
    CHECK(!br_script_check("func f() {\n  sqrt(1, 2)\n}\n", "d.bs", err, sizeof(err)) && strstr(err, "sqrt() takes 1 argument, not 2"));
    CHECK(!br_script_check("print(1)\n", "e.bs", err, sizeof(err)) && strstr(err, "only var, const, func"));
    CHECK(!br_script_check("var s = \"open\n", "f.bs", err, sizeof(err)) && strstr(err, "unfinished string"));
    CHECK(!br_script_check("func f() { var a = 1 2 }\n", "g.bs", err, sizeof(err)) && strstr(err, "end of the line"));
}

static void test_language(void) {
    br_entity e;
    char s[256];
    setup();
    write_script("lang.bs",
        "# everything the language does, checked from C\n"
        "var fib10\nvar text\nvar listsum\nvar vecx\nvar loops\nvar logic\nvar mod\nvar neg\nvar elifs\n"
        "var nested = [1, [2, 3]]\n"
        "func fib(n) {\n"
        "    if n < 2 { return n }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "func classify(n) {\n"
        "    if n < 0 { return \"neg\" } elif n == 0 { return \"zero\" } else { return \"pos\" }\n"
        "}\n"
        "func start() {\n"
        "    fib10 = fib(10)\n"
        "    text = \"score: \" + 42 + \" \" + true + \" \" + vec2(1, 2)\n"
        "    var l = [1, 2, 3]\n"
        "    push(l, 4)\n"
        "    l[0] = 10\n"
        "    listsum = 0\n"
        "    for x in l { listsum += x }\n"
        "    listsum += l[-1] * 100 + len(l) * 1000 + nested[1][0] * 10000\n"
        "    var v = vec2(3, 4) * 2 + vec2(1, 1)\n"
        "    v.x += 1\n"
        "    vecx = v.x * 100 + length(vec2(3, 4))\n"
        "    loops = 0\n"
        "    var i = 0\n"
        "    while true {\n"
        "        i += 1\n"
        "        if i % 2 == 0 { continue }\n"
        "        if i > 9 { break }\n"
        "        loops += 1\n"
        "    }\n"
        "    for k in 3 { loops += 100 }\n"
        "    logic = (nil or 5) + (0 and 99) + (not false and 10 or 0)\n"
        "    mod = -7 % 3\n"
        "    neg = -vec2(1, 2).y\n"
        "    elifs = classify(-1) + classify(0) + classify(5)\n"
        "    print(\"done\", fib10)\n"
        "}\n");
    e = scripted("lang", "lang.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(e, "fib10") == 55);
    CHECK(br_script_get_string(&W, e, "text", s, sizeof(s)) && strcmp(s, "score: 42 true (1, 2)") == 0);
    CHECK(num(e, "listsum") == 10 + 2 + 3 + 4 + 400 + 4000 + 20000);
    CHECK(num(e, "vecx") == 800 + 5);
    CHECK(num(e, "loops") == 5 + 300);                     /* 1 3 5 7 9, then 3 more */
    CHECK(num(e, "logic") == 5 + 0 + 10);
    CHECK(num(e, "mod") == 2);                             /* like Python: never negative */
    CHECK(num(e, "neg") == -2);
    CHECK(br_script_get_string(&W, e, "elifs", s, sizeof(s)) && strcmp(s, "negzeropos") == 0);
    CHECK(strcmp(g_printed, "done 55") == 0);
    teardown();
}

/* ---- structs and classes, access, inheritance, static, const, unsigned ---- */

static void test_classes(void) {
    br_entity e, e2;
    char s[512];
    setup();
    write_script("classes.bs",
        "@strict\n"
        "struct Point {\n"
        "    var x: number = 0\n"
        "    var y: number = 0\n"
        "    func moved(dx: number, dy: number) -> Point { return Point(this.x + dx, this.y + dy) }\n"
        "    func nudge() -> void { this.x += 1 }\n"
        "}\n"
        "class Animal {\n"
        "    public var name: string = \"?\"\n"
        "    protected var legs: uint = 4\n"
        "    private var secret: int = 7\n"
        "    static var count: uint = 0\n"
        "    static const KINGDOM: string = \"animalia\"\n"
        "    const born: int = 0\n"
        "    func init(name: string) -> void {\n"
        "        this.name = name\n"
        "        this.born = Animal.count\n"
        "        Animal.count += 1\n"
        "    }\n"
        "    func speak() -> string { return \"...\" }\n"
        "    func describe() -> string { return this.name + \" says \" + this.speak() + \" on \" + this.legs + \" legs\" }\n"
        "    func peek() -> int { return this.secret }\n"
        "    static func total() -> uint { return Animal.count }\n"
        "}\n"
        "class Dog : Animal {\n"
        "    var tricks: list[string] = []\n"
        "    func init(name: string, trick: string) -> void {\n"
        "        super.init(name)\n"
        "        push(this.tricks, trick)\n"
        "    }\n"
        "    override func speak() -> string { return \"woof\" }\n"
        "}\n"
        "class Bird : Animal {\n"
        "    func init(name: string) -> void { super.init(name)\n this.legs = 2 }\n"
        "    override func speak() -> string { return \"tweet (\" + super.speak() + \")\" }\n"
        "}\n"
        "class Node {\n"
        "    var value: int = 0\n"
        "    var next: Node = nil\n"
        "}\n"
        "var texts: string = \"\"\n"
        "var copies: string = \"\"\n"
        "var total: number = 0\n"
        "var kingdom: string = \"\"\n"
        "var chain: int = 0\n"
        "var shown: string = \"\"\n"
        "var same: string = \"\"\n"
        "var pet: Animal = nil\n"
        "var where: Point\n"
        "func start() -> void {\n"
        "    var zoo: list[Animal] = [Dog(\"Rex\", \"sit\"), Bird(\"Tweety\"), Animal(\"Blob\")]\n"
        "    for a: Animal in zoo { texts += a.describe() + \"; \" }\n"
        "    total = Animal.total() * 100 + zoo[1].born * 10 + zoo[0].peek()\n"
        "    kingdom = Animal.KINGDOM\n"
        "    pet = zoo[0]\n"
        "    var p: Point = Point(1, 2)\n"
        "    var q: Point = p\n"
        "    q.x = 10\n"
        "    var r: Point = p.moved(5, 5)\n"
        "    p.nudge()\n"
        "    var pts: list[Point] = [p]\n"
        "    pts[0].nudge()\n"
        "    for t: Point in pts { t.x = 99 }\n"
        "    copies = str(p.x) + \" \" + q.x + \" \" + r.x + \" \" + pts[0].x\n"
        "    var head: Node = Node()\n"
        "    head.next = Node()\n"
        "    head.next.value = 5\n"
        "    var alias: Node = head\n"
        "    alias.value = 3\n"
        "    chain = head.value * 10 + head.next.value\n"
        "    shown = str(Point(1, 2))\n"
        "    same = str(Point(1, 2) == Point(1, 2)) + \" \" + (Node() == Node()) + \" \" + (head == alias)\n"
        "    where.y = 4\n"
        "}\n");
    e = scripted("zoo", "classes.bs", NULL, br_v2(0, 0));
    steps(1);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    CHECK(br_script_get_string(&W, e, "texts", s, sizeof(s)) &&
          strcmp(s, "Rex says woof on 4 legs; Tweety says tweet (...) on 2 legs; Blob says ... on 4 legs; ") == 0);
    CHECK(num(e, "total") == 300 + 10 + 7);
    CHECK(br_script_get_string(&W, e, "kingdom", s, sizeof(s)) && strcmp(s, "animalia") == 0);
    CHECK(br_script_get_string(&W, e, "copies", s, sizeof(s)) && strcmp(s, "2 10 6 3") == 0);
    CHECK(num(e, "chain") == 35);
    CHECK(br_script_get_string(&W, e, "shown", s, sizeof(s)) && strcmp(s, "Point(x: 1, y: 2)") == 0);
    CHECK(br_script_get_string(&W, e, "same", s, sizeof(s)) && strcmp(s, "true false true") == 0);

    /* statics belong to the file: a second entity with the script shares them */
    e2 = scripted("zoo2", "classes.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    CHECK(num(e2, "total") == 600 + 40 + 7);

    teardown();
}

static void test_class_garbage(void) {
    br_entity e;
    char s[128];
    setup();
    /* objects kept in statics, globals, lists and other objects survive the collector; the rest go */
    write_script("objchurn.bs",
        "struct V {\n  var a: number = 0\n  var tag: string = \"\"\n}\n"
        "class Box {\n  var v: V\n  var next: Box = nil\n  static var kept: list[Box] = []\n}\n"
        "var head: Box = nil\nvar n: int = 0\nvar check: string = \"\"\n"
        "func update(dt: number) -> void {\n"
        "    for i in 300 {\n"
        "        var b := Box()\n"
        "        b.v = V(i, \"tag \" + i)\n"
        "        if i == 7 { b.next = head\n head = b }\n"
        "    }\n"
        "    n += 1\n"
        "    if n % 50 == 0 {\n"
        "        var k := Box()\n"
        "        k.v.tag = \"kept \" + n\n"
        "        push(Box.kept, k)\n"
        "        check = Box.kept[0].v.tag + \" \" + head.v.tag + \" \" + head.next.v.a\n"
        "    }\n"
        "}\n");
    e = scripted("churn", "objchurn.bs", NULL, br_v2(0, 0));
    steps(400);                                            /* ~120000 objects and strings, mostly garbage */
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    CHECK(br_script_get_string(&W, e, "check", s, sizeof(s)) && strcmp(s, "kept 50 tag 7 7") == 0);
    teardown();                                            /* the leak check covers the heap */
}

static void test_class_errors(void) {
    char err[256];
    static const struct { const char *src, *want; } bad[] = {
        { "class A {\n  private var x = 1\n}\nfunc f() {\n  var a := A()\n  print(a.x)\n}\n", "'x' is private in A" },
        { "class A {\n  protected var x = 1\n}\nfunc f() {\n  var a: A = A()\n  a.x = 2\n}\n", "'x' is protected in A" },
        { "class A {\n  private func f() { }\n}\nclass B : A {\n  func g() { this.f() }\n}\n", "'f' is private in A" },
        { "class A {\n  func f() { }\n}\nclass B : A {\n  func f() { }\n}\n", "write override func f()" },
        { "class A {\n  func f() { }\n}\nclass B : A {\n  override func g() { }\n}\n", "g() overrides nothing" },
        { "class A {\n  func f(x: int) { }\n}\nclass B : A {\n  override func f(x: string) { }\n}\n", "same parameters" },
        { "class A {\n  func f() { }\n}\nclass B : A {\n  private override func f() { }\n}\n", "can't be private" },
        { "class A {\n  static func f() { }\n}\nclass B : A {\n  override func f() { }\n}\n", "static functions can't be overridden" },
        { "class A {\n  var x = 1\n}\nclass B : A {\n  var x = 2\n}\n", "already has 'x' from A" },
        { "struct S {\n  var x = 1\n}\nclass B : S {\n}\n", "structs can't be inherited" },
        { "struct S : T {\n}\n", "structs can't inherit" },
        { "class A : B {\n}\nclass B : A {\n}\n", "inherits from itself" },
        { "class A : Nope {\n}\n", "unknown class 'Nope'" },
        { "struct S {\n  var s: S\n}\n", "can't hold itself" },
        { "struct S {\n  protected var x = 1\n}\n", "nothing in them is protected" },
        { "class A {\n  const c = 1\n  func f() { this.c = 2 }\n}\n", "only A's init() can set it" },
        { "class A {\n  static const C = 1\n}\nfunc f() {\n  A.C = 2\n}\n", "A.C is const" },
        { "class A {\n  static var n = 0\n}\nfunc f() {\n  var a = A()\n  print(A.x)\n}\n", "A has no member 'x'" },
        { "class A {\n  var n = 0\n}\nfunc f() {\n  print(A.n)\n}\n", "belongs to each A" },
        { "class A {\n  func f() { }\n}\nfunc g() {\n  A.f()\n}\n", "belongs to each A" },
        { "class A {\n  static func f() { print(this) }\n}\n", "a static function has no this" },
        { "func f() {\n  print(this)\n}\n", "this is only inside" },
        { "class A {\n  func f() { print(self) }\n}\n", "self (the script's entity)" },
        { "var speed = 3\nclass A {\n  func f() { print(speed) }\n}\n", "can't see the script's variables" },
        { "func helper() { }\nclass A {\n  func f() { helper() }\n}\n", "can't call the script's functions" },
        { "class A {\n  var x = this\n}\n", "can't use this" },
        { "class A {\n  func init(x: int) { }\n}\nfunc f() {\n  var a = A()\n  var b = A(1, 2)\n}\n", "A() takes 1 argument, not 2" },
        { "class A {\n}\nfunc f() {\n  var a = A(1)\n}\n", "takes no arguments" },
        { "class A {\n}\nfunc f() {\n  var a = A\n}\n", "make one with A(...)" },
        { "class A {\n}\nclass B {\n}\nfunc f() {\n  var a: A = B()\n}\n", "a is an A, so it can't hold a B" },
        { "struct S {\n}\nfunc f() {\n  var s: S = nil\n}\n", "can't be nil" },
        { "class A {\n  private func init() { }\n}\nfunc f() {\n  var a = A()\n}\n", "only its own functions can make one" },
        { "class A {\n  func f() { super.f() }\n}\n", "no parent class" },
        { "class A {\n}\nclass A {\n}\n", "already is a struct or class 'A'" },
        { "class A {\n}\nfunc A() { }\n", "function called A" },
        { "func f() {\n  class B {\n  }\n}\n", "only be defined at the top" },
        { "static var x = 1\n", "for the members of a struct or class" },
        { "const x = 1\nfunc f() {\n  x = 2\n}\n", "x is const" },
        { "func f() {\n  const y = 1\n  y += 1\n}\n", "y is const" },
        { "const z: int\n", "a const needs its value" },
        { "var u: uint = -1\n", "unsigned, so it can't hold a negative number" },
        { "var u: unsigned string = \"\"\n", "unsigned is only for number types" },
        { "var l: list[unsigned int] = [1]\nfunc f() {\n  push(l, -2)\n}\n", "push(): the list holds unsigned int" },
        { "@strict\nclass A {\n  var x = 1\n}\n", "@strict: give 'x' a type" },
        { "@strict\nclass A {\n  func f() { }\n}\n", "@strict: say what f() returns" },
    };
    size_t i;
    setup();
    for (i = 0; i < BR_ARRAY_COUNT(bad); ++i) {
        bool ok = br_script_check(bad[i].src, "c.bs", err, sizeof(err));
        if (ok || !strstr(err, bad[i].want)) printf("      case %d: %s\n      wanted: %s\n", (int)i, ok ? "(no error)" : err, bad[i].want);
        CHECK(!ok && strstr(err, bad[i].want));
    }
    /* and the good ones */
    CHECK(br_script_check("class A {\n  func f() -> int { return 1 }\n}\nclass B : A {\n  public override func f() -> int { return 2 }\n}\n", "g.bs", err, sizeof(err)));
    CHECK(br_script_check("class T {\n  var next: T\n  var items: list[T] = []\n}\nstruct P {\n  var t: T\n}\n", "g.bs", err, sizeof(err)));
    CHECK(br_script_check("var a: Later = nil\nclass Later {\n}\n", "g.bs", err, sizeof(err)));
    CHECK(br_script_check("const var c := 3\nvar u: uint = 3\nvar f: unsigned float = 0.5\nvar w: unsigned int = 2\n", "g.bs", err, sizeof(err)));
    teardown();
}

static void test_class_runtime_checks(void) {
    br_entity e;
    setup();
    /* untyped code meets private fields, const fields and unsigned values at run time */
    write_script("rt1.bs", "class A {\n  private var x = 1\n}\nfunc start() {\n  var a = [A()][0]\n  print(a.x)\n}\n");
    write_script("rt2.bs", "class A {\n  const c = 1\n}\nfunc start() {\n  var a = [A()][0]\n  a.c = 5\n}\n");
    write_script("rt3.bs", "var u: uint = 0\nfunc start() {\n  u -= 1\n}\n");
    write_script("rt4.bs", "class A {\n  var n: unsigned number = 0\n}\nfunc start() {\n  var a = A()\n  a.n = 0 - 0.5\n}\n");
    write_script("rt5.bs", "class A {\n}\nclass B {\n}\nfunc take(a: A) { }\nfunc start() {\n  var b = [B()][0]\n  take(b)\n}\n");
    write_script("rt6.bs", "class A {\n}\nfunc start() {\n  save_data(\"objects\", A())\n}\n");
    write_script("rt7.bs", "class A {\n  func f() { }\n}\nfunc start() {\n  var a = [A()][0]\n  a.g()\n}\n");
    write_script("rt8.bs", "const k = 3\n");
    write_script("rt8b.bs", "func start() {\n  find(\"k\").k = 4\n}\n");
    e = scripted("a", "rt1.bs", NULL, br_v2(0, 0)); steps(1);
    CHECK(strstr(br_script_last_error(&W), "'x' is private in A"));
    br_entity_destroy(&W, e);
    e = scripted("a", "rt2.bs", NULL, br_v2(0, 0)); steps(1);
    CHECK(strstr(br_script_last_error(&W), "'c' is const"));
    br_entity_destroy(&W, e);
    e = scripted("a", "rt3.bs", NULL, br_v2(0, 0)); steps(1);
    CHECK(strstr(br_script_last_error(&W), "u is unsigned, and -1 is below 0"));
    br_entity_destroy(&W, e);
    e = scripted("a", "rt4.bs", NULL, br_v2(0, 0)); steps(1);
    CHECK(strstr(br_script_last_error(&W), "n is unsigned, and -0.5 is below 0"));
    br_entity_destroy(&W, e);
    e = scripted("a", "rt5.bs", NULL, br_v2(0, 0)); steps(1);
    CHECK(strstr(br_script_last_error(&W), "take()'s parameter 'a' is an A, so it can't hold a B"));
    br_entity_destroy(&W, e);
    e = scripted("a", "rt6.bs", NULL, br_v2(0, 0)); steps(1);
    CHECK(strstr(br_script_last_error(&W), "struct or class object"));
    br_entity_destroy(&W, e);
    e = scripted("a", "rt7.bs", NULL, br_v2(0, 0)); steps(1);
    CHECK(strstr(br_script_last_error(&W), "A has no function g()"));
    br_entity_destroy(&W, e);
    scripted("k", "rt8.bs", NULL, br_v2(0, 0));
    scripted("b", "rt8b.bs", NULL, br_v2(0, 0)); steps(1);
    CHECK(strstr(br_script_last_error(&W), "k is const"));
    teardown();
}

static void test_entities_and_vars(void) {
    br_entity e;
    const br_shape *shape;
    setup();
    write_script("mover.bs",
        "var speed = 10\nvar label = \"none\"\nvar updates = 0\n"
        "func start() {\n"
        "    self.position = vec2(5, 6)\n"
        "    self.Shape.color = rgb(255, 0, 0)\n"
        "    self.Shape.kind = \"ellipse\"\n"
        "    self.Shape.size.x = 30\n"
        "    self.name = \"Moved\"\n"
        "}\n"
        "func update(dt) {\n"
        "    updates += 1\n"
        "    self.x += speed\n"
        "    if self.Sprite != nil { speed = 0 }\n"
        "}\n");
    e = scripted("mover", "mover.bs", "speed = 3, label = \"hi\"", br_v2(0, 0));
    br_add(&W, e, ID.shape, NULL);
    steps(4);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    CHECK(num(e, "updates") == 4 && num(e, "speed") == 3);
    CHECK(((br_transform *)br_get(&W, e, ID.transform))->position.x == 5 + 12);
    shape = (const br_shape *)br_get(&W, e, ID.shape);
    CHECK(br_color_eq(shape->color, br_rgb(255, 0, 0)) && shape->kind == BR_SHAPE_ELLIPSE && shape->size.x == 30);
    CHECK(strcmp(((br_name *)br_get(&W, e, ID.name))->text, "Moved") == 0);
    {
        char s[32];
        CHECK(br_script_get_string(&W, e, "label", s, sizeof(s)) && strcmp(s, "hi") == 0);
    }
    CHECK(br_script_set_number(&W, e, "speed", 0));
    steps(1);
    CHECK(((br_transform *)br_get(&W, e, ID.transform))->position.x == 17);
    teardown();
}

static void test_runtime_errors(void) {
    br_entity bad, good, loop;
    setup();
    write_script("bad.bs", "var n = 0\nvar gone\nfunc update(dt) {\n    n += 1\n    if n == 3 { var x = gone + 1 }\n}\n");   /* (nil + 1 itself: caught before it runs) */
    write_script("good.bs", "var n = 0\nfunc update(dt) { n += 1 }\n");
    write_script("loop.bs", "func start() {\n    while true { }\n}\n");
    bad = scripted("bad", "bad.bs", NULL, br_v2(0, 0));
    good = scripted("good", "good.bs", NULL, br_v2(0, 0));
    loop = scripted("loop", "loop.bs", NULL, br_v2(0, 0));
    QUIET(steps(5));
    CHECK(strstr(br_script_last_error(&W), "bad.bs:5: can't use + on nil and a number") != NULL);
    CHECK_EQ_INT(br_script_error_count(&W), 2);            /* each reported once (loop.bs: too long) */
    {
        double v;
        CHECK(!br_script_get_number(&W, bad, "n", &v));      /* stopped */
    }
    CHECK(num(good, "n") == 5);                            /* unaffected */
    BR_UNUSED(loop);
    {   /* a missing file and a typo'd one are reported, nothing crashes */
        scripted("missing", "nope.bs", NULL, br_v2(0, 0));
        write_script("typo.bs", "func update(dt) { self.x += speeed }\n");
        scripted("typo", "typo.bs", NULL, br_v2(0, 0));
        QUIET(steps(2));
        CHECK(strstr(br_script_last_error(&W), "typo.bs:1: unknown name 'speeed'") != NULL);
        CHECK_EQ_INT(br_script_error_count(&W), 4);
    }
    teardown();
}

static void test_physics_callbacks_and_messages(void) {
    br_entity ball, ground, zone, enemy;
    setup();
    write_script("ball.bs",
        "var hits = 0\nvar last = \"\"\nvar zones = 0\nvar damage = 0\n"
        "func on_collision(other, normal, speed) {\n"
        "    hits += 1\n"
        "    last = other.name\n"
        "    if normal.y > 0.9 and speed > 50 { damage = other.hit(3) }\n"
        "}\n"
        "func on_trigger(other) { zones += 1 }\n");
    write_script("ground.bs",
        "var health = 100\n"
        "func hit(amount) {\n"
        "    health -= amount\n"
        "    return health\n"
        "}\n");
    write_script("enemy.bs",
        "var target\n"
        "func start() { target = find(\"Ground\") }\n"
        "func update(dt) {\n"
        "    if target.health < 100 and target.health > 90 { target.health -= 1 }\n"
        "    send(target, \"hit\", 1)\n"
        "    send(target, \"no_such_function\")\n"
        "}\n");
    ground = scripted("Ground", "ground.bs", NULL, br_v2(160, 170));
    ((br_collider *)br_add(&W, ground, COL, NULL))->size = br_v2(300, 20);
    zone = br_entity_create(&W);
    ((br_transform *)br_add(&W, zone, ID.transform, NULL))->position = br_v2(160, 100);
    {
        br_collider *c = (br_collider *)br_add(&W, zone, COL, NULL);
        c->size = br_v2(100, 10);
        c->trigger = true;
    }
    ball = scripted("Ball", "ball.bs", NULL, br_v2(160, 50));
    br_add(&W, ball, RB, NULL);
    {
        br_collider *c = (br_collider *)br_add(&W, ball, COL, NULL);
        c->shape = BR_COLLIDER_CIRCLE;
        c->radius = 5;
    }
    steps(120);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(ball, "hits") == 1 && num(ball, "zones") == 1);
    {
        char s[32];
        CHECK(br_script_get_string(&W, ball, "last", s, sizeof(s)) && strcmp(s, "Ground") == 0);
    }
    CHECK(num(ball, "damage") == 97 && num(ground, "health") == 97);
    enemy = scripted("Enemy", "enemy.bs", NULL, br_v2(0, 0));
    steps(3);                                              /* -1 (send) per step, -1 more while 90..100 */
    CHECK(num(ground, "health") == 97 - 6);
    BR_UNUSED(enemy);
    teardown();
}

static void test_spawn_destroy_input(void) {
    br_entity shooter;
    br_world prefab;
    setup();
    br_world_init(&prefab);
    br_builtin_register(&prefab, NULL);
    {   /* a bullet prefab */
        br_entity b = br_entity_create(&prefab);
        snprintf(((br_name *)br_add(&prefab, b, br_component_find(&prefab, "Name"), NULL))->text, 32, "Bullet");
        br_add(&prefab, b, br_component_find(&prefab, "Transform"), NULL);
        CHECK(br_world_save_file(&prefab, &b, 1, test_path("bullet.scene")));
    }
    br_world_destroy(&prefab);
    write_script("shooter.bs",
        "var fired = 0\n"
        "func update(dt) {\n"
        "    if key_pressed(\"space\") {\n"
        "        var b = spawn(\"bullet.scene\", self.position + vec2(10, 0))\n"
        "        fired += 1\n"
        "    }\n"
        "    if key_down(\"Left\") and len(find_all(\"Bullet\")) > 0 { destroy(find(\"Bullet\")) }\n"
        "}\n");
    shooter = scripted("Shooter", "shooter.bs", NULL, br_v2(50, 50));
    steps(1);
    g_engine.input.keys_pressed[BR_KEY_SPACE] = true;
    steps(1);
    g_engine.input.keys_pressed[BR_KEY_SPACE] = false;
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(shooter, "fired") == 1 && br_world_entity_count(&W) == 2);
    {
        br_entity bullet = W.pools[ID.name].entities[1];
        CHECK(((br_transform *)br_get(&W, bullet, ID.transform))->position.x == 60);
    }
    g_engine.input.keys[BR_KEY_LEFT] = true;
    steps(1);
    CHECK(br_world_entity_count(&W) == 1);                 /* destroyed (deferred to the end of the step) */
    teardown();
}

/* Tilemaps from scripts, substr(), and spawning a big prefab (a room: a
 * root with more than 64 children). */
static void test_tiles_substr_big_spawn(void) {
    br_entity runner, map;
    br_tilemap *m;
    setup();
    {   /* the prefab: a root, 100 children (written by hand, as the generator would) */
        char *text = (char *)br_alloc(20000);
        size_t at = 0;
        int i;
        at += (size_t)snprintf(text + at, 20000 - at, "bronto_scene 1\nentity\n  Name\n    text = \"Room\"\n  Transform\n    position = 0 0\n");
        for (i = 0; i < 100; ++i)
            at += (size_t)snprintf(text + at, 20000 - at, "entity\n  Name\n    text = \"Bit\"\n  Transform\n    position = %d 0\n  Parent\n    entity = 0\n", i);
        CHECK(br_file_write_all(test_path("room_test.scene"), text, at));
        br_free(text);
    }
    map = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, map, ID.name, NULL))->text, 32, "Map");
    ((br_transform *)br_add(&W, map, ID.transform, NULL))->position = br_v2(16, 8);
    m = (br_tilemap *)br_add(&W, map, ID.tilemap, NULL);      /* 2 x 1 cells of 16: x 0..32, y 0..16 */
    m->columns = 2;
    m->rows = 1;
    strcpy(m->tiles, ".#");
    strcpy(m->solid, "#");
    write_script("tiles.bs",
        "var a = \"\"\nvar solid_left = true\nvar solid_right = false\nvar c := vec2(0, 0)\nvar part = \"\"\nvar kids = 0\n"
        "func start() {\n"
        "    var m = find(\"Map\")\n"
        "    a = tile_at(m, vec2(4, 4)) + tile_at(m, vec2(20, 4)) + tile_at(m, vec2(40, 4)) + \"|\"\n"
        "    solid_left = tile_solid(m, vec2(4, 4))\n"
        "    solid_right = tile_solid(m, vec2(20, 4))\n"
        "    c = tile_center(m, vec2(20, 4))\n"
        "    set_tile(m, vec2(4, 4), \"#\")\n"
        "    part = substr(\"recipe\", 2, 3) + substr(\"recipe\", -2) + substr(\"ab\", 5)\n"
        "    var room = spawn(\"room_test.scene\", vec2(10, 20), self)\n"
        "    kids = len(children(room))\n"
        "}\n");
    runner = scripted("Runner", "tiles.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    {
        char buf[64];
        CHECK(br_script_get_string(&W, runner, "a", buf, sizeof(buf)) && strcmp(buf, ".#|") == 0);
        CHECK(br_script_get_string(&W, runner, "part", buf, sizeof(buf)) && strcmp(buf, "cippe") == 0);
    }
    CHECK(num(runner, "solid_left") == 0 && num(runner, "solid_right") == 1);
    CHECK(strcmp(((br_tilemap *)br_get(&W, map, ID.tilemap))->tiles, "##") == 0);
    CHECK(num(runner, "kids") == 100);
    CHECK_EQ_INT(br_world_entity_count(&W), 103);            /* map, runner, room + 100: all parented */
    teardown();
}

/* A spawned entity's script is ready at once: the spawner can set its
 * variables and call it before its start() runs (next step). */
static void test_spawn_then_set(void) {
    br_entity maker;
    setup();
    write_script("ball.bs", "var speed = 1\nvar seen = 0\nvar told = \"\"\nfunc start() {\n    seen = speed\n}\nfunc tell(s) {\n    told = s\n}\n");
    {
        const char text[] = "bronto_scene 1\nentity\n  Name\n    text = \"Ball\"\n  Transform\n    position = 0 0\n  Script\n    file = \"ball.bs\"\n    vars = \"speed = 2\"\n";
        CHECK(br_file_write_all(test_path("ball.scene"), text, sizeof(text) - 1));
    }
    write_script("maker.bs", "var before = 0\nfunc start() {\n    var b = spawn(\"ball.scene\", vec2(5, 5))\n    before = b.speed\n    b.speed = 7\n    b.tell(\"hi\")\n}\n");
    maker = scripted("Maker", "maker.bs", NULL, br_v2(0, 0));
    steps(3);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(maker, "before") == 2);                                  /* the prefab's vars were applied */
    {
        br_entity ball = W.pools[ID.name].entities[1];
        char buf[16];
        CHECK(num(ball, "seen") == 7);                                  /* start() saw what the maker set */
        CHECK(br_script_get_string(&W, ball, "told", buf, sizeof(buf)) && strcmp(buf, "hi") == 0);
    }
    teardown();
}

/* Saved data from scripts: save_data / load_data and friends, and lists in
 * Script.vars (both read values the same way). */
static void test_saved_data(void) {
    char folder[BR_PATH_MAX];
    br_entity e, v;
    int errors;
    setup();
    br_path_join(folder, sizeof(folder), g_out_dir, "script_saves");
    br_save_set_folder(folder);
    br_save_delete("progress.txt");
    br_save_delete("damaged.txt");
    write_script("saver.bs",
                 "var pass = 0\nvar missing = 0\nvar names = 0\nvar after = 0\n"
                 "func start() {\n"
                 "    var v = [3, \"ow_7_7\", vec2(-128.5, 152), true, nil, [1, [2, []]], \"say \\\"hi\\\" \\\\ \\n done\", 0.1, -7]\n"
                 "    if not save_data(\"progress\", v) { return }\n"
                 "    var got = load_data(\"progress\")\n"
                 "    if str(got) == str(v) and got[2].x == -128.5 and got[6] == v[6] and got[7] == 0.1 and len(got[5][1][1]) == 0 { pass = 1 }\n"
                 "    missing = load_data(\"nothing\", 42)\n"
                 "    if load_data(\"nothing\") == nil and has_data(\"progress\") and not has_data(\"nothing\") { missing += 1 }\n"
                 "    names = len(data_names())\n"
                 "    if data_names()[0] == \"damaged\" and data_names()[1] == \"progress\" { names += 10 }\n"
                 "    if load_data(\"damaged\", \"fallback\") == \"fallback\" { names += 100 }\n"
                 "    delete_data(\"progress\")\n"
                 "    if not has_data(\"progress\") and load_data(\"progress\", 5) == 5 { after = 1 }\n"
                 "}\n");
    CHECK(br_save_write("damaged.txt", "[1, 2", 5));
    e = scripted("Saver", "saver.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(e, "pass") == 1);
    CHECK(num(e, "missing") == 43);
    CHECK(num(e, "names") == 112);                       /* damaged and progress, in order; a damaged file gives the default */
    CHECK(num(e, "after") == 1);
    {
        size_t size;
        char *text;
        write_script("saver2.bs", "func start() { save_data(\"plain\", [1, \"a\", vec2(2, 3)]) }\n");
        scripted("Saver2", "saver2.bs", NULL, br_v2(0, 0));
        steps(2);
        text = (char *)br_save_read("plain.txt", &size);      /* readable text, like code */
        CHECK(text && strcmp(text, "[1, \"a\", vec2(2, 3)]\n") == 0);
        br_free(text);
        br_save_delete("plain.txt");
    }
    /* what can't be saved, and names that aren't allowed: errors */
    errors = br_script_error_count(&W);
    write_script("bad_save.bs", "func start() { save_data(\"me\", [self]) }\n");
    scripted("BadSave", "bad_save.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK_EQ_INT(br_script_error_count(&W), errors + 1);
    CHECK(strstr(br_script_last_error(&W), "can't save an entity") != NULL);
    write_script("bad_name.bs", "func start() { load_data(\"../up\") }\n");
    scripted("BadName", "bad_name.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK_EQ_INT(br_script_error_count(&W), errors + 2);
    CHECK(strstr(br_script_last_error(&W), "can't name saved data") != NULL);
    /* lists (and negative vec2s) in Script.vars */
    write_script("holder.bs", "var items: list = []\nvar spot := vec2(0, 0)\nvar ok = 0\n"
                              "func start() { if len(items) == 3 and items[1] == \"two\" and items[2][0] == -1 and spot.y == -4 { ok = 1 } }\n");
    v = scripted("Holder", "holder.bs", "items = [\"one\", \"two\", [-1]], spot = vec2(3, -4)", br_v2(0, 0));
    steps(2);
    CHECK(num(v, "ok") == 1);
    CHECK_EQ_INT(br_script_error_count(&W), errors + 2);
    br_save_delete("damaged.txt");
    br_fs_remove(folder);
    br_save_set_folder(NULL);
    teardown();
}

/* Volume settings and choosing keys from scripts: set_volume / volume on the
 * mixer's buses (a Sound component plays on its bus), and last_key(). */
static void test_volume_and_keys(void) {
    br_entity e;
    double m = 0, mus = 0, master = 0;
    char key[16];
    setup();
    write_script("volume.bs", "var m = 0\nvar music = 0\nvar master = 0\nvar key = \"\"\n"
                              "func start() { set_volume(\"music\", 0.25); set_volume(\"master\", 0.5); m = volume(\"music\"); music = volume(\"sfx\"); master = volume(\"master\") }\n"
                              "func update(dt) { var k = last_key(); if k != \"\" { key = k } }\n");
    e = scripted("Volume", "volume.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK(br_script_get_number(&W, e, "m", &m) && fabs(m - 0.25) < 1e-6);
    CHECK(br_script_get_number(&W, e, "music", &mus) && mus == 1);
    CHECK(br_script_get_number(&W, e, "master", &master) && fabs(master - 0.5) < 1e-6);
    CHECK(fabsf(g_engine.mixer.bus_volume[BR_BUS_MUSIC] - 0.25f) < 1e-6f && fabsf(g_engine.mixer.master_volume - 0.5f) < 1e-6f);
    br_input_begin_frame(&g_engine.input);
    {
        br_event k = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_Q, 0, false);
        br_input_handle_event(&g_engine.input, &k);
    }
    steps(1);
    CHECK(br_script_get_string(&W, e, "key", key, sizeof(key)) && strcmp(key, "q") == 0);
    write_script("badbus.bs", "func start() { set_volume(\"loud\", 1) }\n");
    scripted("BadBus", "badbus.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK(strstr(br_script_last_error(&W), "there is no \"loud\"") != NULL);
    {   /* a Sound component's bus is saved by name */
        const br_component_desc *d = br_component_get_desc(&W, ID.sound);
        int i, found = 0;
        for (i = 0; d && i < d->field_count; ++i) if (strcmp(d->fields[i].name, "bus") == 0) found = 1;
        CHECK(found);
    }
    g_engine.mixer.master_volume = 1;
    g_engine.mixer.bus_volume[BR_BUS_MUSIC] = 1;
    teardown();
}

/* Pantry Quest (examples/pantryquest, a project made of scenes and scripts):
 * Pip walks into the first cave, takes the spoon, and walks back out. */
static br_entity named(const char *name) {
    uint32_t i;
    for (i = 0; i < W.pools[ID.name].count; ++i)
        if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)i * W.pools[ID.name].stride))->text, name) == 0)
            return W.pools[ID.name].entities[i];
    return BR_ENTITY_NULL;
}
static void hold_key(br_key k, int n) {
    g_engine.input.keys[k] = true;
    steps(n);
    g_engine.input.keys[k] = false;
}
static void test_pantryquest_example(void) {
    char err[200], room[32];
    br_entity game, hero;
    double weapon = -1;
    const br_transform *t;
    setup();
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);                        /* (Pantry Quest is strictly typed: project.bronto) */
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    hero = named("Hero");
    steps(40);                                             /* the first room fades in */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "ow_7_7") == 0);
    CHECK(named("Map") != BR_ENTITY_NULL);
    hold_key(BR_KEY_LEFT, 35);                             /* to the cave mouth's column (x 72)... */
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && t->position.x > 69 && t->position.x < 75);
    hold_key(BR_KEY_UP, 70);                               /* ...and up into it */
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "cave_chef") == 0);
    hold_key(BR_KEY_UP, 45);                               /* the spoon */
    CHECK(br_script_get_number(&W, game, "weapon", &weapon) && weapon == 1);
    steps(110);                                            /* holds it up, then can move again */
    CHECK(br_script_get_string(&W, game, "state", room, sizeof(room)) && strcmp(room, "play") == 0);
    hold_key(BR_KEY_DOWN, 90);                             /* and out again */
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "ow_7_7") == 0);
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && br_absf(t->position.x - 72) < 2 && t->position.y > 100);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}

/* Pantry Quest: sliding into a cellar room whose shutter closes behind Pip
 * leaves him clear of the doorway, free to walk (he used to be shut in). */
static void test_pantryquest_shutter(void) {
    char err[200], room[32];
    br_entity game, hero;
    br_script *sc;
    const br_transform *t;
    float x, y;
    setup();
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);                        /* (Pantry Quest is strictly typed: project.bronto) */
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    hero = named("Hero");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    CHECK(sc != NULL);
    if (!sc) { teardown(); return; }
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"d1_2_2\", start_pos = vec2(40, 152), weapon = 1");
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "d1_2_2") == 0);
    hold_key(BR_KEY_LEFT, 40);                             /* out through the west doorway... */
    steps(80);                                             /* ...into the cutter room, whose east shutter closes */
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "d1_1_2") == 0);
    CHECK(br_script_get_string(&W, game, "state", room, sizeof(room)) && strcmp(room, "play") == 0);
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && t->position.x <= 216);                      /* past the doorway's two tiles */
    if (!t) { teardown(); return; }
    x = t->position.x;
    y = t->position.y;
    hold_key(BR_KEY_UP, 10);                               /* and he can walk */
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && t->position.y < y - 4);
    hold_key(BR_KEY_LEFT, 10);
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && t->position.x < x - 4);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}

/* Pantry Quest's overworld (examples/pantryquest/rooms, made by its tools):
 * 16 x 8 screens whose edges meet, sealed all round, with all nine cellar
 * doors. */
static bool pq_field(const char *text, const char *key, char *out, size_t size) {
    const char *p = strstr(text, key), *q;
    if (!p || !(p = strchr(p, '"')) || !(q = strchr(p + 1, '"')) || (size_t)(q - p - 1) >= size) return false;
    memcpy(out, p + 1, (size_t)(q - p - 1));
    out[q - p - 1] = 0;
    return true;
}
static void test_pantryquest_world(void) {
    static char tiles[16][8][16 * 11 + 1];
    char solid[64] = "", path[96];
    int x, y, i, k, cellars = 0;
    bool ok = true;
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 16; ++x) {
            size_t size;
            char *text;
            snprintf(path, sizeof(path), "examples/pantryquest/rooms/ow_%d_%d.scene", x, y);
            text = (char *)br_file_read_all(path, &size);
            CHECK(text != NULL);
            if (!text) return;
            CHECK(pq_field(text, "    tiles = ", tiles[x][y], sizeof(tiles[x][y])) && strlen(tiles[x][y]) == 16 * 11);
            pq_field(text, "    solid = ", solid, sizeof(solid));
            for (k = 1; k <= 9; ++k) {
                char door[32];
                snprintf(door, sizeof(door), "to = \\\"d%d_2_4\\\"", k);
                if (strstr(text, door)) cellars |= 1 << k;
            }
            br_free(text);
        }
    }
    CHECK(solid[0] != 0);
#define PQ_OPEN(xx, yy, c, r) (strchr(solid, tiles[xx][yy][(r) * 16 + (c)]) == NULL)
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 16; ++x) {
            for (i = 0; i < 16; ++i) {                          /* top and bottom edges */
                if (y == 0 && PQ_OPEN(x, y, i, 0)) ok = false;
                if (y == 7 && PQ_OPEN(x, y, i, 10)) ok = false;
                if (y < 7 && PQ_OPEN(x, y, i, 10) != PQ_OPEN(x, y + 1, i, 0)) ok = false;
            }
            for (i = 0; i < 11; ++i) {                          /* left and right */
                if (x == 0 && PQ_OPEN(x, y, 0, i)) ok = false;
                if (x == 15 && PQ_OPEN(x, y, 15, i)) ok = false;
                if (x < 15 && PQ_OPEN(x, y, 15, i) != PQ_OPEN(x + 1, y, 0, i)) ok = false;
            }
            if (!ok) { printf("      ow_%d_%d's edges don't meet its neighbours'\n", x, y); CHECK(ok); return; }
        }
    }
#undef PQ_OPEN
    CHECK_EQ_INT(cellars, 0x3fe);                               /* cellars 1 to 9 */
}

/* Pantry Quest's cellar guardians, fought from here: Pip stays put (his
 * health kept full) and pokes at the guardian every few steps. The Big Dill
 * only feels it while dizzy from charging into a wall; the Jammerwock only
 * while it is up out of the jam; the Spice Wraith only while it is solid.
 * Each must go down, and the cellar's music comes back. `only` lists the
 * modes a hit may land in (NULL: any). */
static void pq_fight(const char *room, int max_hp, const char *only) {
    char err[200], mode[32], music[32];
    br_entity game, boss = BR_ENTITY_NULL;
    br_script *sc;
    double hp = 0, last_hp, life = 32;
    int i, landed = 0, shrugged = 0;
    static const double poke[3] = { 1, 0, 0 };
    setup();
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);                        /* (Pantry Quest is strictly typed: project.bronto) */
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"%s\", start_pos = vec2(128, 200), weapon = 1, hearts_max = 16", room);
    steps(40);
    boss = named("Enemy");
    CHECK(boss != BR_ENTITY_NULL);
    if (boss == BR_ENTITY_NULL) { teardown(); return; }
    last_hp = max_hp;
    for (i = 0; i < 60 * 60 && br_entity_alive(&W, boss); ++i) {
        br_script_set_number(&W, game, "hp", life);
        steps(1);
        if (i % 7 || !br_entity_alive(&W, boss)) continue;
        mode[0] = 0;
        br_script_get_string(&W, boss, "mode", mode, sizeof(mode));
        br_script_call(&W, boss, "hit", poke, 1);   /* (the amount: the rest take their defaults) */
        if (!br_entity_alive(&W, boss)) { landed++; break; }
        br_script_get_number(&W, boss, "hp", &hp);
        if (hp < last_hp) {
            landed++;
            if (only) CHECK(mode[0] && strstr(only, mode) != NULL);
        } else if (only && !(mode[0] && strstr(only, mode))) {
            shrugged++;
        }
        last_hp = hp;
    }
    CHECK(!br_entity_alive(&W, boss));
    CHECK_EQ_INT(landed, max_hp);
    if (only) CHECK(shrugged > 0);
    steps(2);
    CHECK(br_script_get_string(&W, game, "music_name", music, sizeof(music)) && strcmp(music, "cellar") == 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}
/* Guardians in pieces, fought by poking at every enemy in the room until
 * the guardian is gone; its bar (hp) must count down `total` hits, and at
 * most `pieces` other enemies are about at once.
 *  - The Gumdrop King: four hits and he bursts into two gumdrops, two hits
 *    each and those burst into two little ones; one hit pops each of those.
 *  - The Lucky Cookie: eight crumbs circle it, and while any is left its
 *    shell turns every hit.
 *  - The Noodle Hydra: an iron pot no hit dents, with four heads; three hits
 *    break each off (to fly loose, beyond hurting) and the last takes the pot. */
static void pq_fight_all(const char *room, int total, int pieces) {
    char err[200], music[32];
    br_entity game, king, all[32];
    br_script *sc;
    double hp = 0, last_hp = total, life = 32;
    int i, j, n, landed = 0, most = 0;
    static const double poke[3] = { 1, 0, 0 };
    setup();
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);                        /* (Pantry Quest is strictly typed: project.bronto) */
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"%s\", start_pos = vec2(128, 200), weapon = 1, hearts_max = 16", room);
    steps(40);
    king = named("Enemy");
    CHECK(king != BR_ENTITY_NULL);
    if (king == BR_ENTITY_NULL) { teardown(); return; }
    steps(60 * 4);                                     /* (his entrance) */
    CHECK(br_script_get_number(&W, king, "hp", &hp) && hp == total);
    for (i = 0; i < 60 * 60 && br_entity_alive(&W, king); ++i) {
        br_script_set_number(&W, game, "hp", life);
        steps(1);
        if (i % 9) continue;
        n = 0;
        for (j = 0; j < (int)W.pools[ID.name].count && n < 32; ++j)
            if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)j * W.pools[ID.name].stride))->text, "Enemy") == 0)
                all[n++] = W.pools[ID.name].entities[j];
        if (n - 1 > most) most = n - 1;                /* (the king stays, unseen, to keep count) */
        for (j = 0; j < n; ++j) br_script_call(&W, all[j], "hit", poke, 1);   /* (the amount: the rest take their defaults) */
        steps(1);
        if (!br_entity_alive(&W, king)) { landed += (int)last_hp; break; }
        br_script_get_number(&W, king, "hp", &hp);
        if (hp < last_hp) landed += (int)(last_hp - hp);
        last_hp = hp;
    }
    CHECK(!br_entity_alive(&W, king));
    CHECK_EQ_INT(landed, total);
    CHECK_EQ_INT(most, pieces);
    steps(2);
    CHECK(br_script_get_string(&W, game, "music_name", music, sizeof(music)) && strcmp(music, "cellar") == 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}
static void test_pantryquest_bosses(void) {
    pq_fight("d2_2_1", 6, "dizzy");             /* the Big Dill */
    pq_fight("d3_1_0", 8, NULL);                /* the Spice Wraith */
    pq_fight("d4_2_0", 10, "up rise");           /* the Jammerwock */
    pq_fight_all("d5_2_0", 12, 4);              /* the Gumdrop King */
    pq_fight_all("d6_2_0", 10, 8);               /* the Lucky Cookie */
    pq_fight("d7_2_0", 12, "stuck");             /* the Sugar Golem */
    pq_fight_all("d8_1_0", 20, 4);              /* the Noodle Hydra */
}

/* Pantry Quest: Pip walks in eight directions - a diagonal as fast as
 * straight - and is eased round a corner he only just touches, so he slips
 * into a cave mouth without lining up first. */
static void test_pantryquest_eight_ways(void) {
    char err[200], room[32];
    br_entity game, hero;
    br_script *sc;
    const br_transform *t;
    float x0, y0;
    setup();
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);                        /* (Pantry Quest is strictly typed: project.bronto) */
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    hero = named("Hero");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"ow_7_7\", start_pos = vec2(120, 168)");
    steps(40);
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    if (!t) { teardown(); return; }
    x0 = t->position.x;
    y0 = t->position.y;
    g_engine.input.keys[BR_KEY_UP] = g_engine.input.keys[BR_KEY_LEFT] = true;     /* up and to the left */
    steps(30);
    g_engine.input.keys[BR_KEY_UP] = g_engine.input.keys[BR_KEY_LEFT] = false;
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && x0 - t->position.x > 26 && x0 - t->position.x < 33);   /* 84 px/s for 0.5 s, shared between the axes */
    CHECK(t && y0 - t->position.y > 26 && y0 - t->position.y < 33);
    CHECK(t && fabsf((x0 - t->position.x) - (y0 - t->position.y)) < 1);
    /* four pixels right of the first cave's mouth (column 4: x 64 to 80), walking straight up */
    teardown();
    setup();
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);                        /* (Pantry Quest is strictly typed: project.bronto) */
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"ow_7_7\", start_pos = vec2(76, 120)");
    steps(40);
    hold_key(BR_KEY_UP, 60);
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "cave_chef") == 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}

/* Pantry Quest's title, files and settings, played with key presses: start
 * a new game in file 1, save it from the pause menu, quit to the title and
 * find the file there; then choose a new key and a softer volume. */
static void pq_press(br_key k) {
    g_engine.input.keys_pressed[k] = g_engine.input.keys[k] = true;
    g_engine.input.last_device = BR_DEVICE_KEYBOARD;
    steps(1);
    g_engine.input.keys_pressed[k] = g_engine.input.keys[k] = false;
    steps(2);
}
static bool pq_load(const char *scene) {
    char err[200], path[96];
    setup();
    br_assets_set_root("examples/pantryquest/");
    br_script_set_strict(true);                        /* (Pantry Quest is strictly typed: project.bronto) */
    snprintf(path, sizeof(path), "examples/pantryquest/%s", scene);
    return br_world_load_file(&W, path, NULL, 0, err, sizeof(err)) > 5;
}
/* Pantry Quest's wafer plank and whisk. With the plank Pip walks over jam
 * one tile wide (the Jam Jar's moat room), and not without it; the whisk,
 * out on the land, whirls him to the door of a cellar whose page he has. */
static float pq_walk_west(const char *treasures) {
    br_script *sc;
    const br_transform *t;
    float x;
    if (!pq_load("main.scene")) { teardown(); return -1; }
    sc = (br_script *)br_get(&W, named("Game"), SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"d4_0_2\", start_pos = vec2(200, 152), weapon = 1, treasures = [%s]", treasures);
    steps(40);
    hold_key(BR_KEY_LEFT, 90);
    t = (const br_transform *)br_get(&W, named("Hero"), ID.transform);
    x = t ? t->position.x : -1;
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
    return x;
}
/* The oven mitt: below the crate that plugs the gap in the Fortune Hall's
 * wall of crates, Pip leans on it (pushing it up a tile) and walks on
 * through the gap and out of the east door; without the mitt, he can't. */
static void pq_mitt(bool mitt, char *room, size_t size) {
    br_script *sc;
    br_entity game;
    room[0] = 0;
    if (!pq_load("main.scene")) { teardown(); return; }
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"d6_1_1\", start_pos = vec2(168, 164), weapon = 1, treasures = [%s]",
             mitt ? "\"mitt\"" : "");
    steps(40);
    hold_key(BR_KEY_UP, 30);
    hold_key(BR_KEY_RIGHT, 80);
    steps(60);
    br_script_get_string(&W, game, "room_name", room, size);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}
static void test_pantryquest_plank_and_whisk(void) {
    char room32[32];
    pq_mitt(false, room32, sizeof(room32));
    CHECK(strcmp(room32, "d6_1_1") == 0);
    pq_mitt(true, room32, sizeof(room32));
    CHECK(strcmp(room32, "d6_2_1") == 0);
    char room[32];
    br_entity game;
    br_script *sc;
    float x;
    x = pq_walk_west("");
    CHECK(x > 128);                                    /* stopped at the jam (x 112 to 128) */
    x = pq_walk_west("\"plank\"");
    CHECK(x > 0 && x < 100);                           /* over it on the plank */
    CHECK(pq_load("main.scene"));
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"ow_7_7\", start_pos = vec2(120, 168), start_items = \"whisk\", "
             "flags = [\"page:d1\", \"page:d3\"]");
    steps(40);
    pq_press(BR_KEY_Z);
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "ow_7_5") == 0);   /* the Cold Cellar's door */
    pq_press(BR_KEY_Z);
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "ow_1_6") == 0);   /* then the Spice Rack's */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}

/* Pantry Quest's last cellar, Baron Gorge's Banquet Hall. */
static br_entity pq_start(const char *room, float x, float y, const char *more) {
    br_script *sc;
    br_entity game;
    if (!pq_load("main.scene")) return BR_ENTITY_NULL;
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"%s\", start_pos = vec2(%g, %g), weapon = 1, hearts_max = 16%s%s",
             room, (double)x, (double)y, more[0] ? ", " : "", more);
    steps(40);
    return game;
}
static void pq_end(void) {
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}
static void pq_place_hero(int col, int row) {          /* Pip in an inside cell (12 x 7) of the room */
    br_transform *t = (br_transform *)br_get(&W, named("Hero"), ID.transform);
    if (t) {
        t->position.x = (float)((col + 2) * 16 + 8);
        t->position.y = (float)(64 + (row + 2) * 16 + 8 - 4);
    }
}
static br_key pq_key_of(char d) {
    return d == 'U' ? BR_KEY_UP : d == 'D' ? BR_KEY_DOWN : d == 'L' ? BR_KEY_LEFT : BR_KEY_RIGHT;
}
static double pq_num(br_entity e, const char *var) {
    double v = -1;
    br_script_get_number(&W, e, var, &v);
    return v;
}
static int pq_count(const char *name) {
    uint32_t i;
    int n = 0;
    for (i = 0; i < W.pools[ID.name].count; ++i)
        if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)i * W.pools[ID.name].stride))->text, name) == 0) n++;
    return n;
}

/* The door in only opens for a cook with all eight recipe pages. */
static void test_pantryquest_banquet_gate(void) {
    char room[32];
    br_entity game;
    int pages;
    for (pages = 7; pages <= 8; ++pages) {
        char more[32];
        snprintf(more, sizeof(more), "pages = %d", pages);
        game = pq_start("d9_2_4", 128, 200, more);
        hold_key(BR_KEY_UP, 150);
        steps(60);
        CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)));
        CHECK(strcmp(room, pages == 8 ? "d9_2_3" : "d9_2_4") == 0);
        pq_end();
    }
}

/* The crates-and-plates rooms, solved the way tools found (the fewest
 * pushes): Pip set beside each crate in turn, leaning on it. Each push must
 * move the crate a cell, and the last solves the room, opening its sealed
 * door for good. */
typedef struct { int col, row; char dir; } pq_push;
static void pq_crates(const char *room, const pq_push *plan, int n, const char *sealed_cell_check) {
    br_entity game;
    int i;
    (void)sealed_cell_check;
    game = pq_start(room, 128, 152, "treasures = [\"mitt\"]");
    CHECK(pq_num(game, "hp") > 0);
    for (i = 0; i < n; ++i) {
        CHECK(pq_num(named("Room"), "solved") == 0);
        pq_place_hero(plan[i].col, plan[i].row);
        steps(2);
        hold_key(pq_key_of(plan[i].dir), 25);
        steps(4);
    }
    CHECK(pq_num(named("Room"), "solved") == 1);
    pq_end();
}
static void test_pantryquest_banquet_puzzles(void) {
    static const pq_push west[] = { {8, 1, 'D'}, {4, 2, 'L'}, {8, 4, 'U'}, {7, 2, 'R'}, {2, 3, 'U'}, {2, 2, 'U'}, {9, 3, 'U'}, {9, 2, 'U'} };
    static const pq_push east[] = { {3, 2, 'L'}, {1, 3, 'U'}, {1, 2, 'U'}, {8, 2, 'R'}, {10, 3, 'U'}, {10, 2, 'U'} };
    char room[32];
    br_entity game, b[4];
    uint32_t i;
    int n = 0;
    pq_crates("d9_1_3", west, 8, NULL);
    pq_crates("d9_4_3", east, 6, NULL);
    /* the braziers: the fire chili lights one for real; all four at once solves the room... */
    game = pq_start("d9_1_2", 56, 172, "start_items = \"chili\"");
    pq_place_hero(1, 4);
    g_engine.input.keys[BR_KEY_UP] = true; steps(1); g_engine.input.keys[BR_KEY_UP] = false;
    steps(2);
    pq_press(BR_KEY_Z);
    steps(30);
    for (i = 0; i < W.pools[ID.name].count && n < 4; ++i)
        if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)i * W.pools[ID.name].stride))->text, "Brazier") == 0)
            b[n++] = W.pools[ID.name].entities[i];
    CHECK_EQ_INT(n, 4);
    if (n == 4) {
        CHECK_EQ_INT((int)(pq_num(b[0], "lit") + pq_num(b[1], "lit") + pq_num(b[2], "lit") + pq_num(b[3], "lit")), 1);
        steps(60 * 10);                                /* ...but on its own it burns out */
        CHECK_EQ_INT((int)(pq_num(b[0], "lit") + pq_num(b[1], "lit") + pq_num(b[2], "lit") + pq_num(b[3], "lit")), 0);
        for (i = 0; i < 4; ++i) { br_script_call(&W, b[i], "light", NULL, 0); steps(60); }
        CHECK(pq_num(named("Room"), "solved") == 1);
    }
    pq_end();
    /* the gong: push the crate out of the skewer's way, then shoot along the row */
    game = pq_start("d9_3_2", 200, 152, "treasures = [\"mitt\"], start_items = \"skewers\", beans = 5");
    pq_place_hero(4, 3);
    hold_key(BR_KEY_UP, 25);
    steps(4);
    pq_place_hero(6, 2);
    g_engine.input.keys[BR_KEY_LEFT] = true; steps(1); g_engine.input.keys[BR_KEY_LEFT] = false;
    steps(2);
    pq_press(BR_KEY_Z);
    steps(60);
    CHECK(pq_num(named("Gong"), "rung") == 1);
    CHECK(pq_num(named("Room"), "solved") == 1);
    hold_key(BR_KEY_UP, 10);                           /* (the sealed door north is open now) */
    pq_place_hero(5, 0);
    hold_key(BR_KEY_UP, 60);
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "d9_3_1") == 0);
    pq_end();
}

/* A gauntlet: the foyer's three waves come in one after the other, and only
 * after the last do its shutters open. (Its enemies are simply removed.) An
 * enemy hit the moment it appears, before it has started, just shrugs. */
static void test_pantryquest_banquet_waves(void) {
    br_entity game;
    int i, j, most_spawners, waves_seen = 0, last = -1;
    uint32_t k;
    game = pq_start("d9_2_3", 128, 200, "");
    most_spawners = pq_count("Spawner");
    CHECK_EQ_INT(most_spawners, 6);
    CHECK(pq_num(named("Room"), "shutting") == 1);
    for (i = 0; i < 60 * 30 && (pq_count("Spawner") > 0 || pq_count("Enemy") > 0); ++i) {
        br_script_set_number(&W, game, "hp", 32);
        steps(1);
        if (pq_count("Spawner") != last) {
            static const double poke[3] = { 1, 0, 0 };
            if (last >= 0) {                           /* a wave just came in: hit it before it has started */
                for (k = 0; k < W.pools[ID.name].count; ++k)
                    if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)k * W.pools[ID.name].stride))->text, "Enemy") == 0)
                        br_script_call(&W, W.pools[ID.name].entities[k], "hit", poke, 1);   /* (the amount: the rest take their defaults) */
                CHECK_EQ_INT(br_script_error_count(&W), 0);
            }
            last = pq_count("Spawner");
            waves_seen++;
        }
        if (i % 10) continue;
        for (j = 0; j < 8; ++j) {
            br_entity e = BR_ENTITY_NULL;
            for (k = 0; k < W.pools[ID.name].count; ++k)
                if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)k * W.pools[ID.name].stride))->text, "Enemy") == 0) {
                    e = W.pools[ID.name].entities[k];
                    break;
                }
            if (e == BR_ENTITY_NULL) break;
            br_entity_destroy(&W, e);
        }
    }
    steps(10);
    CHECK_EQ_INT(waves_seen, 3);                       /* 6 held back, then 3, then none */
    CHECK_EQ_INT(pq_count("Enemy"), 0);
    CHECK(pq_num(named("Room"), "shutting") == 0);
    pq_end();
}

/* Baron Gorge, all three courses: hit while he leans over the table; fed a
 * bomb while he breathes in, then hit while he is stunned; hit when his
 * dish cover lifts. Down he goes, and the Grand Recipe behind him ends the
 * game. */
static void test_pantryquest_baron(void) {
    static const double poke[3] = { 1, 0, 0 };
    char mode[32], music[32], scene[64];
    br_entity game, baron;
    double hp, phase, most_phase = 0;
    int i, fed = 0, hits_open = 0;
    game = pq_start("d9_2_0", 128, 200, "bombs = 8, start_items = \"bombs\"");
    baron = named("Enemy");
    CHECK(baron != BR_ENTITY_NULL);
    if (baron == BR_ENTITY_NULL) { pq_end(); return; }
    for (i = 0; i < 60 * 240 && br_entity_alive(&W, baron); ++i) {
        br_script_set_number(&W, game, "hp", 32);
        br_script_set_number(&W, game, "bombs", 8);
        steps(1);
        if (!br_entity_alive(&W, baron)) break;
        mode[0] = 0;
        br_script_get_string(&W, baron, "mode", mode, sizeof(mode));
        phase = pq_num(baron, "phase");
        if (phase > most_phase) most_phase = phase;
        if (strcmp(mode, "inhale") == 0 && pq_count("Bomb") == 0 && i % 20 == 0) {   /* a bomb at his feet */
            br_transform *bt = (br_transform *)br_get(&W, baron, ID.transform);
            br_transform *ht = (br_transform *)br_get(&W, named("Hero"), ID.transform);
            if (bt && ht) { ht->position.x = bt->position.x; ht->position.y = bt->position.y + 64 + 30; }
            pq_press(BR_KEY_Z);
            fed++;
        }
        if (i % 7) continue;
        hp = pq_num(baron, "hp");
        br_script_call(&W, baron, "hit", poke, 1);   /* (the amount: the rest take their defaults) */
        if (br_entity_alive(&W, baron) && pq_num(baron, "hp") < hp) hits_open++;
    }
    CHECK(!br_entity_alive(&W, baron));
    CHECK_EQ_INT((int)most_phase, 3);
    CHECK(fed > 0);
    CHECK(hits_open >= 39);
    steps(2);
    CHECK(br_script_get_string(&W, game, "music_name", music, sizeof(music)) && strcmp(music, "cellar") == 0);
    pq_end();
    /* the Grand Recipe: the end, and back to the title */
    game = pq_start("d9_3_0", 128, 160, "");
    hold_key(BR_KEY_UP, 40);
    steps(60 * 4);
    CHECK(br_script_get_string(&W, game, "state", mode, sizeof(mode)) && strcmp(mode, "won") == 0);
    CHECK(named("Ending") != BR_ENTITY_NULL);
    steps(60 * 4);
    pq_press(BR_KEY_ENTER);
    CHECK(br_script_scene_request(&W, scene, sizeof(scene)) && strcmp(scene, "title.scene") == 0);
    pq_end();
}

/* Upgrades: a copper spoon waits in its cave until Pip has five hearts;
 * each spoon or ladle hits harder; the chef's jacket halves every hurt, the
 * royal coat quarters it. */
static void pq_hero_at(float x, float y) {
    br_transform *t = (br_transform *)br_get(&W, named("Hero"), ID.transform);
    if (t) { t->position.x = x; t->position.y = y; }
}
static void test_pantryquest_upgrades(void) {
    static const double two[1] = { 2 };
    br_entity game;
    double hp;
    int hearts;
    for (hearts = 4; hearts <= 5; ++hearts) {
        char more[48];
        snprintf(more, sizeof(more), "hearts_max = %d", hearts);
        if (!pq_load("main.scene")) { teardown(); return; }
        game = named("Game");
        snprintf(((br_script *)br_get(&W, game, SCRIPT))->vars, sizeof(((br_script *)0)->vars),
                 "start_room = \"cave_spoon2\", start_pos = vec2(128, 200), weapon = 1, %s", more);
        steps(40);
        hold_key(BR_KEY_UP, 40);
        steps(150);
        CHECK_EQ_INT((int)pq_num(game, "weapon"), hearts == 5 ? 2 : 1);
        pq_end();
    }
    /* how hard he hits, and how much a hit hurts */
    game = pq_start("ow_7_7", 120, 168, "weapon = 4, outfit = 1");
    hp = pq_num(game, "hp");
    CHECK(br_script_call(&W, game, "damage", two, 1));
    CHECK(pq_num(game, "hp") == hp - 1);               /* a whole heart's hit, halved */
    hp = pq_num(game, "hp");
    CHECK(pq_num(game, "weapon") == 4);
    CHECK(br_script_call(&W, game, "attack", NULL, 0));
    br_script_set_number(&W, game, "outfit", 2);
    CHECK(br_script_call(&W, game, "damage", two, 1));
    CHECK(pq_num(game, "hp") == hp - 0.5);
    pq_end();
}

/* The old chef's hints: on the "WHERE NEXT?" card, the first cellar whose page
 * Pip hasn't found (or, for a hidden one, to get bombs first); on the
 * "UPGRADES?" card, an upgrade he can reach now, a different one each time. */
static void pq_card(br_entity game, float x, char *said, size_t size) {
    pq_hero_at(128, 120);                              /* off the cards... */
    steps(3);
    pq_hero_at(x, 192);                                /* ...and onto one */
    steps(3);
    br_script_get_string(&W, game, "say1", said, size);
}
static void test_pantryquest_hints(void) {
    char said[64];
    br_entity game;
    game = pq_start("cave_chef", 128, 200, "hearts_max = 3, flags = [\"spoon\"]");
    pq_card(game, 64, said, sizeof(said));
    CHECK(strcmp(said, "THE COLD CELLAR, JUST NORTH") == 0);
    pq_card(game, 192, said, sizeof(said));
    CHECK(strcmp(said, "A HEART PIE: FAR NORTHWEST,") == 0);
    pq_card(game, 192, said, sizeof(said));
    CHECK(strcmp(said, "A HEART PIE: NORTH,") == 0);
    pq_end();
    game = pq_start("cave_chef", 128, 200, "hearts_max = 3, flags = [\"page:d1\", \"page:d2\", \"page:d3\", \"page:d4\", "
                    "\"page:d5\", \"page:d6\", \"ow:pie1\", \"ow:pie2\", \"ow:pie3\", \"pie2\"]");
    pq_card(game, 64, said, sizeof(said));
    CHECK(strcmp(said, "FIRST, GET POPCORN BOMBS:") == 0);     /* the Sugar Mill is behind a cracked cliff */
    pq_card(game, 192, said, sizeof(said));
    CHECK(strcmp(said, "NOTHING YOU CAN REACH YET.") == 0);    /* the rest need bombs, fire or more hearts */
    pq_end();
    game = pq_start("cave_chef", 128, 200, "hearts_max = 3, start_items = \"bombs\", flags = [\"page:d1\", \"page:d2\", "
                    "\"page:d3\", \"page:d4\", \"page:d5\", \"page:d6\"]");
    pq_card(game, 64, said, sizeof(said));
    CHECK(strcmp(said, "THE SUGAR MILL: A CRACKED") == 0);
    pq_end();
}

/* Enemies get tougher with the stage of the game: a toast knight in the Jam
 * Jar (stage 4) against one in the Banquet Hall (stage 9). */
static void pq_toast(const char *room, double *hp, double *damage) {
    char image[32];
    uint32_t i;
    *hp = *damage = -1;
    pq_start(room, 128, 200, "");
    for (i = 0; i < W.pools[ID.name].count; ++i) {
        br_entity e = W.pools[ID.name].entities[i];
        if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)i * W.pools[ID.name].stride))->text, "Enemy") == 0 &&
            br_script_get_string(&W, e, "image", image, sizeof(image)) && strcmp(image, "art/toast_") == 0) {
            *hp = pq_num(e, "hp");
            *damage = pq_num(e, "damage");
            break;
        }
    }
    pq_end();
}
static void test_pantryquest_stages(void) {
    double hp, damage;
    pq_toast("d4_2_3", &hp, &damage);
    CHECK(hp == 3 && damage == 3);                     /* 2 and 2, x1.5 */
    pq_toast("d9_2_3", &hp, &damage);
    CHECK(hp == 7 && damage == 6);                     /* x3.5 and x3 */
}

/* The Pocket Pantry and the shortcuts: E and Q cycle the item on B without
 * opening anything; keys belong to the cellar they're found in (out on the
 * land Pip has none; back in, his are there again); the mouse over the
 * pantry's equipment and treasures says what they are, over a plate it
 * chooses that item, and a click on the MAP tab turns the page. Older
 * settings (from before the shortcuts) keep their keys, and a new shortcut
 * loses its key if an old action already uses it. */
static void pq_mouse(float x, float y, bool click) {
    g_engine.input.mouse_x = x;
    g_engine.input.mouse_y = y;
    g_engine.input.mouse_pressed[BR_MOUSE_LEFT] = g_engine.input.mouse[BR_MOUSE_LEFT] = click;
    steps(1);
    g_engine.input.mouse_pressed[BR_MOUSE_LEFT] = g_engine.input.mouse[BR_MOUSE_LEFT] = false;
    steps(2);
}
static const char *pq_label(const char *name) {
    const br_label *l = (const br_label *)br_get(&W, named(name), ID.label);
    return l ? l->text : "";
}
static void test_pantryquest_pocket_pantry(void) {
    char folder[BR_PATH_MAX], first[32], text[32], room[32];
    const char *old = "[[[\"up\", \"w\"], [\"down\", \"s\"], [\"left\", \"a\"], [\"right\", \"d\"], [\"e\", \"space\"], "
                      "[\"z\", \"j\"], [\"enter\", \"i\"], [\"escape\", \"p\"]], 8, 10]";
    br_entity game;
    br_path_join(folder, sizeof(folder), g_out_dir, "pq_pantry");
    br_save_set_folder(folder);
    br_save_delete("settings.txt");
    game = pq_start("d1_2_4", 128, 200, "start_items = \"bombs,cutter,skewers\", treasures = [\"plank\"]");
    /* E and Q: the next and the previous item, in play */
    CHECK(br_script_get_string(&W, game, "b_item", first, sizeof(first)) && first[0]);
    pq_press(BR_KEY_E);
    CHECK(br_script_get_string(&W, game, "b_item", text, sizeof(text)) && strcmp(text, first) != 0);
    pq_press(BR_KEY_Q);
    CHECK(br_script_get_string(&W, game, "b_item", text, sizeof(text)) && strcmp(text, first) == 0);
    pq_press(BR_KEY_E); pq_press(BR_KEY_E); pq_press(BR_KEY_E);    /* three items: round again */
    CHECK(br_script_get_string(&W, game, "b_item", text, sizeof(text)) && strcmp(text, first) == 0);
    CHECK(named("Menu") == BR_ENTITY_NULL && named("MapView") == BR_ENTITY_NULL);
    /* the mouse in the Pocket Pantry (at 128, 152) */
    pq_press(BR_KEY_ENTER);
    CHECK(named("Menu") != BR_ENTITY_NULL);
    pq_mouse(128 - 96, 152 - 17, false);                          /* the outfit */
    CHECK(strcmp(pq_label("ItemName"), "COOK'S APRON") == 0);
    pq_mouse(128 - 96, 152 - 36, false);                          /* the spoon */
    CHECK(strcmp(pq_label("ItemName"), "WOODEN SPOON") == 0);
    pq_mouse(128 + 96, 152 - 36, false);                          /* a treasure */
    CHECK(strcmp(pq_label("ItemName"), "WAFER PLANK") == 0 && strcmp(pq_label("ItemInfo"), "CROSS JAM ONE TILE WIDE") == 0);
    pq_mouse(128 + 21, 152 - 30, false);                          /* the third plate: chosen for B */
    CHECK(br_script_get_string(&W, game, "b_item", text, sizeof(text)) && strcmp(text, "skewers") == 0);
    CHECK(strcmp(pq_label("ItemName"), "SKEWER BOW") == 0);
    pq_mouse(128 + 64, 152 - 73, true);                           /* a click on MAP */
    CHECK(named("Menu") == BR_ENTITY_NULL && named("MapView") != BR_ENTITY_NULL);
    pq_mouse(128 - 24, 152 - 79, true);                           /* and on POCKET PANTRY */
    CHECK(named("Menu") != BR_ENTITY_NULL && named("MapView") == BR_ENTITY_NULL);
    pq_press(BR_KEY_ENTER);
    CHECK(named("Menu") == BR_ENTITY_NULL);
    CHECK(br_script_get_string(&W, game, "state", text, sizeof(text)) && strcmp(text, "play") == 0);
    /* keys: this cellar's two stay in it */
    br_script_set_number(&W, game, "keys", 2);
    hold_key(BR_KEY_DOWN, 70);
    steps(60);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strncmp(room, "ow_", 3) == 0);
    CHECK(pq_num(game, "keys") == 0);
    hold_key(BR_KEY_UP, 70);
    steps(60);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "d1_2_4") == 0);
    CHECK(pq_num(game, "keys") == 2);
    pq_end();
    /* older settings: E was the spoon's, so NEXT ITEM has no key; Q still works */
    CHECK(br_save_write("settings.txt", old, strlen(old)));
    game = pq_start("d1_2_4", 128, 200, "start_items = \"bombs,cutter\"");
    CHECK(br_script_get_string(&W, game, "b_item", first, sizeof(first)) && first[0]);
    pq_press(BR_KEY_E);
    CHECK(br_script_get_string(&W, game, "b_item", text, sizeof(text)) && strcmp(text, first) == 0);
    pq_press(BR_KEY_Q);
    CHECK(br_script_get_string(&W, game, "b_item", text, sizeof(text)) && strcmp(text, first) != 0);
    pq_end();
    br_save_delete("settings.txt");
    br_fs_remove(folder);
    br_save_set_folder(NULL);
}

/* Pantry Quest with a gamepad: the stick walks, the shoulder buttons change
 * the item, Start pauses and B resumes, X opens the Pocket Pantry and the
 * shoulder buttons turn its page, Y closes the map; the hints name the
 * buttons; and the settings change a button with the gamepad. */
static void pq_button(br_gamepad_button b) {
    g_engine.input.pads[0].connected = true;
    g_engine.input.pads[0].pressed[b] = g_engine.input.pads[0].buttons[b] = true;
    g_engine.input.last_device = BR_DEVICE_GAMEPAD;
    steps(1);
    g_engine.input.pads[0].pressed[b] = g_engine.input.pads[0].buttons[b] = false;
    steps(2);
}
static void test_pantryquest_gamepad(void) {
    char folder[BR_PATH_MAX], first[32], text[64];
    br_entity game;
    const br_transform *t;
    float x0;
    size_t size;
    char *saved;
    br_path_join(folder, sizeof(folder), g_out_dir, "pq_pad");
    br_save_set_folder(folder);
    br_save_delete("settings.txt");
    game = pq_start("d1_2_4", 128, 200, "start_items = \"bombs,cutter\"");
    t = (const br_transform *)br_get(&W, named("Hero"), ID.transform);
    x0 = t ? t->position.x : 0;
    g_engine.input.pads[0].connected = true;
    g_engine.input.pads[0].axes[BR_PAD_AXIS_LEFT_X] = -1;          /* the stick, left */
    steps(30);
    g_engine.input.pads[0].axes[BR_PAD_AXIS_LEFT_X] = 0;
    steps(2);
    t = (const br_transform *)br_get(&W, named("Hero"), ID.transform);
    CHECK(t && t->position.x < x0 - 20);
    CHECK(br_script_get_string(&W, game, "b_item", first, sizeof(first)) && first[0]);
    pq_button(BR_PAD_RIGHT_BUMPER);                                 /* the next item */
    CHECK(br_script_get_string(&W, game, "b_item", text, sizeof(text)) && strcmp(text, first) != 0);
    pq_button(BR_PAD_LEFT_BUMPER);
    CHECK(br_script_get_string(&W, game, "b_item", text, sizeof(text)) && strcmp(text, first) == 0);
    pq_button(BR_PAD_START);                                       /* pause; B resumes */
    CHECK(named("PauseMenu") != BR_ENTITY_NULL && strcmp(pq_label("Hint"), "B: BACK TO THE GAME") == 0);
    pq_button(BR_PAD_B);
    CHECK(named("PauseMenu") == BR_ENTITY_NULL);
    pq_button(BR_PAD_X);                                           /* the Pocket Pantry, its hints in buttons */
    CHECK(named("Menu") != BR_ENTITY_NULL && strcmp(pq_label("Keys"), "LB/RB: MAP   X: CLOSE") == 0);
    pq_button(BR_PAD_RIGHT_BUMPER);                                 /* the other page */
    CHECK(named("Menu") == BR_ENTITY_NULL && named("MapView") != BR_ENTITY_NULL);
    CHECK(strcmp(pq_label("Keys"), "LB/RB: ITEMS  Y: CLOSE") == 0);
    pq_button(BR_PAD_Y);
    CHECK(named("MapView") == BR_ENTITY_NULL);
    CHECK(br_script_get_string(&W, game, "state", text, sizeof(text)) && strcmp(text, "play") == 0);
    pq_press(BR_KEY_ENTER);                                        /* a key: the hints are keys again */
    CHECK(named("Menu") != BR_ENTITY_NULL && strcmp(pq_label("Keys"), "TAB: MAP   ENTER: CLOSE") == 0);
    pq_press(BR_KEY_ENTER);
    pq_end();
    /* the settings, with the gamepad: UP's button becomes LB (and PREVIOUS ITEM loses it) */
    CHECK(pq_load("title.scene"));
    steps(4);
    g_engine.input.pads[0].connected = true;
    pq_button(BR_PAD_DPAD_DOWN);
    pq_button(BR_PAD_A);                                           /* SETTINGS */
    CHECK(named("SettingsMenu") != BR_ENTITY_NULL);
    CHECK(strcmp(pq_label("KeyA0"), "D-UP") == 0 && strcmp(pq_label("KeyB0"), "") == 0);
    pq_button(BR_PAD_DPAD_DOWN);
    pq_button(BR_PAD_DPAD_DOWN);                                   /* UP */
    pq_button(BR_PAD_A);
    steps(10);
    pq_button(BR_PAD_LEFT_BUMPER);
    CHECK(strcmp(pq_label("KeyA0"), "LB") == 0);
    pq_button(BR_PAD_B);                                           /* leave: saved */
    CHECK(named("SettingsMenu") == BR_ENTITY_NULL);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
    saved = (char *)br_save_read("settings.txt", &size);
    CHECK(saved && strstr(saved, "[\"lb\", \"down\", \"left\", \"right\", \"a\", \"b\", \"x\", \"start\", \"back\", \"y\", \"rb\", \"\"]"));
    br_free(saved);
    memset(g_engine.input.pads, 0, sizeof(g_engine.input.pads));
    br_save_delete("settings.txt");
    br_fs_remove(folder);
    br_save_set_folder(NULL);
}

static void test_pantryquest_files_and_settings(void) {
    char folder[BR_PATH_MAX], text[64];
    size_t size;
    char *saved;
    double slot = 0;
    br_path_join(folder, sizeof(folder), g_out_dir, "pq_saves");
    br_save_set_folder(folder);
    br_save_delete("slot1.txt"); br_save_delete("session.txt"); br_save_delete("settings.txt");
    /* the title: PLAY, then file 1 (empty: a new game) */
    CHECK(pq_load("title.scene"));
    steps(4);
    pq_press(BR_KEY_ENTER);
    CHECK(named("FilesMenu") != BR_ENTITY_NULL);
    pq_press(BR_KEY_ENTER);
    CHECK(br_script_scene_request(&W, text, sizeof(text)) && strcmp(text, "main.scene") == 0);
    CHECK(br_save_exists("session.txt"));
    teardown();
    /* the game: file 1 is saved at once; pause, save, then save and quit */
    CHECK(pq_load("main.scene"));
    steps(60);
    CHECK(br_script_get_number(&W, named("Game"), "slot", &slot) && slot == 1);
    CHECK(br_save_exists("slot1.txt") && !br_save_exists("session.txt"));
    br_script_set_number(&W, named("Game"), "beans", 42);          /* something to find again */
    pq_press(BR_KEY_M);                                           /* the MAP key: where Pip is, the rest in fog */
    CHECK(named("MapView") != BR_ENTITY_NULL);
    {
        const br_label *where = (const br_label *)br_get(&W, named("Where"), ID.label);
        CHECK(where && strcmp(where->text, "YOU ARE ON THE PANTRY PLAINS") == 0);
        CHECK_EQ_INT(pq_count("MapFog"), 127);                   /* a new game: only the first screen seen */
    }
    pq_press(BR_KEY_TAB);                                         /* the Pocket Pantry's other page: the items */
    CHECK(named("MapView") == BR_ENTITY_NULL && named("Menu") != BR_ENTITY_NULL);
    pq_press(BR_KEY_TAB);                                         /* and back */
    CHECK(named("MapView") != BR_ENTITY_NULL && named("Menu") == BR_ENTITY_NULL);
    pq_press(BR_KEY_M);                                           /* (closes it, back to play) */
    CHECK(named("MapView") == BR_ENTITY_NULL);
    CHECK(br_script_get_string(&W, named("Game"), "state", text, sizeof(text)) && strcmp(text, "play") == 0);
    steps(2);
    CHECK(named("MapView") == BR_ENTITY_NULL);                    /* (and it didn't open again) */
    br_script_set_number(&W, named("Game"), "beans", 41);
    pq_press(BR_KEY_F5);                                          /* QUICK SAVE, in play */
    {
        const br_label *note = (const br_label *)br_get(&W, named("BannerText"), ID.label);
        CHECK(note && strcmp(note->text, "GAME SAVED!") == 0);
        saved = (char *)br_save_read("slot1.txt", &size);
        CHECK(saved && strstr(saved, "\"beans\", 41"));
        br_free(saved);
    }
    br_script_set_number(&W, named("Game"), "beans", 42);
    pq_press(BR_KEY_ESCAPE);
    CHECK(named("PauseMenu") != BR_ENTITY_NULL);
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_ENTER);                                       /* SAVE GAME */
    CHECK(br_script_get_string(&W, named("Game"), "state", text, sizeof(text)) && strcmp(text, "pause") == 0);
    {
        const br_label *note = (const br_label *)br_get(&W, named("Note"), ID.label);
        CHECK(note && strcmp(note->text, "GAME SAVED!") == 0);
    }
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_ENTER);                                       /* QUIT TO TITLE: save first? */
    pq_press(BR_KEY_ENTER);                                       /* SAVE AND QUIT */
    CHECK(br_script_scene_request(&W, text, sizeof(text)) && strcmp(text, "title.scene") == 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
    saved = (char *)br_save_read("slot1.txt", &size);
    CHECK(saved && strstr(saved, "\"room\", \"ow_7_7\"") && strstr(saved, "\"place\", \"PANTRY PLAINS\""));
    br_free(saved);
    /* the title again: file 1 shows where it is; then the settings */
    CHECK(pq_load("title.scene"));
    steps(4);
    pq_press(BR_KEY_ENTER);
    {
        const br_label *place = (const br_label *)br_get(&W, named("Place0"), ID.label);
        CHECK(place && strcmp(place->text, "PANTRY PLAINS") == 0);
    }
    pq_press(BR_KEY_ENTER);                                       /* carry on with file 1 */
    CHECK(br_script_scene_request(&W, text, sizeof(text)) && strcmp(text, "main.scene") == 0);
    teardown();
    {
        double beans = 0;
        CHECK(pq_load("main.scene"));
        steps(60);
        CHECK(br_script_get_number(&W, named("Game"), "beans", &beans) && beans == 42);
        CHECK(br_script_get_string(&W, named("Game"), "room_name", text, sizeof(text)) && strcmp(text, "ow_7_7") == 0);
        teardown();
    }
    CHECK(pq_load("title.scene"));
    steps(4);
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_ENTER);                                       /* SETTINGS */
    CHECK(named("SettingsMenu") != BR_ENTITY_NULL);
    pq_press(BR_KEY_LEFT);                                        /* the music, one step softer */
    CHECK(fabsf(g_engine.mixer.bus_volume[BR_BUS_MUSIC] - 0.7f) < 1e-4f);
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_DOWN);                                        /* UP */
    pq_press(BR_KEY_ENTER);
    steps(10);
    pq_press(BR_KEY_I);                                           /* is now I */
    pq_press(BR_KEY_ESCAPE);                                      /* leave: saved */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
    saved = (char *)br_save_read("settings.txt", &size);
    CHECK(saved && strncmp(saved, "[[[\"i\", \"w\"]", 12) == 0 && strstr(saved, "], 7, 10, ["));
    CHECK(saved && strstr(saved, "[\"enter\", \"\"]"));          /* I was the Pocket Pantry's: a key does one thing */
    br_free(saved);
    g_engine.mixer.bus_volume[BR_BUS_MUSIC] = g_engine.mixer.bus_volume[BR_BUS_SFX] = g_engine.mixer.bus_volume[BR_BUS_UI] = 1;
    br_save_delete("slot1.txt"); br_save_delete("settings.txt");
    br_fs_remove(folder);
    br_save_set_folder(NULL);
}

/* Gamepads in scripts: buttons by name (and other names), the triggers as
 * buttons, sticks and axes, which pad, choosing a button, and rumble. */
static void pad_frame(const br_event *evs, int count) {
    int i;
    br_input_begin_frame(&g_engine.input);
    for (i = 0; i < count; ++i) br_input_handle_event(&g_engine.input, &evs[i]);
    br_input_end_events(&g_engine.input);
    steps(1);
}
static br_event pad_event(uint32_t type, int pad, int code, float value) {
    br_event e = br_event_make(type);
    e.u.gamepad.pad = pad;
    e.u.gamepad.button = code;
    e.u.gamepad.axis = code;
    e.u.gamepad.value = value;
    e.u.gamepad.name = "Test Pad";
    return e;
}
static void test_gamepad_input(void) {
    br_entity e;
    char text[64];
    float lo = 0, hi = 0;
    br_event ev[4];
    setup();
    g_engine.platform = br_platform_headless();
    {
        br_gamepad_raw raw;
        memset(&raw, 0, sizeof(raw));
        raw.connected = true;                                      /* (so the headless pad can rumble) */
        br_headless_set_gamepad(0, &raw);
    }
    write_script("pad.bs",
        "var down = 0\nvar pressed = 0\nvar released = 0\nvar on_one = 0\nvar lt = 0\nvar mx = 0\nvar my = 0\nvar rt = 0\n"
        "var count = 0\nvar name = \"\"\nvar last = \"\"\nvar dpad = 0\nvar shook = 0\nvar right = 0\n"
        "func update(dt) {\n"
        "    if button_down(\"a\") { down += 1 }\n"
        "    if button_pressed(\"cross\") { pressed += 1 }\n"
        "    if button_released(\"A\", 0) { released += 1 }\n"
        "    if button_pressed(\"a\", 1) { on_one += 1 }\n"
        "    if button_pressed(\"l2\") { lt += 1 }\n"
        "    if button_down(\"left\") { dpad += 1 }\n"
        "    var s = stick()\n"
        "    mx = s.x\n"
        "    my = s.y\n"
        "    right = stick(\"right\", 0).x\n"
        "    rt = axis(\"rt\")\n"
        "    count = gamepad_count()\n"
        "    name = gamepad_name()\n"
        "    var b = last_button()\n"
        "    if b != \"\" { last = b }\n"
        "    if button_pressed(\"start\") and rumble(1, 0.5) { shook += 1 }\n"
        "    if not gamepad_connected(2) and gamepad_connected() { count += 10 }\n"
        "    name = name + \"/\" + last_input()\n"
        "}\n");
    e = scripted("Pad", "pad.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK(num(e, "count") == 0 && num(e, "mx") == 0);           /* no pads yet */
    ev[0] = pad_event(BR_EVENT_GAMEPAD_CONNECTED, 0, 0, 0);
    ev[1] = pad_event(BR_EVENT_GAMEPAD_BUTTON_DOWN, 0, BR_PAD_A, 0);
    pad_frame(ev, 2);
    CHECK(num(e, "down") == 1 && num(e, "pressed") == 1 && num(e, "on_one") == 0);
    CHECK(num(e, "count") == 11);
    CHECK(br_script_get_string(&W, e, "name", text, sizeof(text)) && strcmp(text, "Test Pad/gamepad") == 0);
    CHECK(br_script_get_string(&W, e, "last", text, sizeof(text)) && strcmp(text, "a") == 0);
    pad_frame(NULL, 0);
    CHECK(num(e, "down") == 2 && num(e, "pressed") == 1);         /* held: pressed only once */
    ev[0] = pad_event(BR_EVENT_GAMEPAD_BUTTON_UP, 0, BR_PAD_A, 0);
    ev[1] = pad_event(BR_EVENT_GAMEPAD_AXIS, 0, BR_PAD_AXIS_LEFT_TRIGGER, 0.9f);
    ev[2] = pad_event(BR_EVENT_GAMEPAD_AXIS, 0, BR_PAD_AXIS_LEFT_X, 1.0f);
    ev[3] = pad_event(BR_EVENT_GAMEPAD_AXIS, 0, BR_PAD_AXIS_RIGHT_TRIGGER, 0.5f);
    pad_frame(ev, 4);
    CHECK(num(e, "released") == 1 && num(e, "down") == 2 && num(e, "lt") == 1);
    CHECK(num(e, "mx") == 1 && num(e, "my") == 0 && num(e, "right") == 0);
    CHECK_NEAR(num(e, "rt"), 0.375, 1e-6);                       /* (0.5, past the 0.2 dead zone, rescaled) */
    CHECK(br_script_get_string(&W, e, "last", text, sizeof(text)) && strcmp(text, "lt") == 0);
    ev[0] = pad_event(BR_EVENT_GAMEPAD_BUTTON_DOWN, 1, BR_PAD_A, 0);    /* a second pad */
    ev[1] = pad_event(BR_EVENT_GAMEPAD_BUTTON_DOWN, 1, BR_PAD_DPAD_LEFT, 0);
    ev[2] = pad_event(BR_EVENT_GAMEPAD_BUTTON_DOWN, 0, BR_PAD_START, 0);
    pad_frame(ev, 3);
    CHECK(num(e, "on_one") == 1 && num(e, "pressed") == 2 && num(e, "dpad") == 1);   /* "a" without a pad: any */
    CHECK(num(e, "count") == 12 && num(e, "shook") == 1);
    br_headless_get_rumble(0, &lo, &hi);
    CHECK(lo == 1 && hi > 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    {   /* mistakes: a button that isn't, a pad that can't be, and the wrong number of arguments */
        br_entity bad;
        write_script("pad_bad.bs", "func update(dt) {\n    if button_down(\"z\") { print(1) }\n}\n");
        bad = scripted("Bad", "pad_bad.bs", NULL, br_v2(0, 0));
        steps(1);
        CHECK(br_script_error_count(&W) >= 1 && strstr(br_script_last_error(&W), "no gamepad button called \"z\""));
        br_entity_destroy(&W, bad);
        write_script("pad_bad2.bs", "func update(dt) {\n    var v = axis(\"left_x\", 7)\n}\n");
        bad = scripted("Bad2", "pad_bad2.bs", NULL, br_v2(0, 0));
        steps(1);
        CHECK(strstr(br_script_last_error(&W), "gamepads are numbered 0 to 3") != NULL);
        br_entity_destroy(&W, bad);
        CHECK(!br_script_check("func update(dt) {\n    rumble(1)\n}\n", "x.bs", text, sizeof(text)) && strstr(text, "rumble() takes 2 to 3"));
    }
    br_gamepad_rumble(&g_engine, 0, 0, 0, 0);
    {   /* what was touched last: a key, then the mouse (a real move, not a nudge), then the gamepad again */
        br_event k = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_SPACE, 0, false);
        br_input_handle_event(&g_engine.input, &k);
        CHECK(g_engine.input.last_device == BR_DEVICE_KEYBOARD);
        k = br_event_mouse_move(10, 10, 1, 0);
        br_input_handle_event(&g_engine.input, &k);
        CHECK(g_engine.input.last_device == BR_DEVICE_KEYBOARD);
        k = br_event_mouse_move(20, 10, 10, 0);
        br_input_handle_event(&g_engine.input, &k);
        CHECK(g_engine.input.last_device == BR_DEVICE_MOUSE);
        k = pad_event(BR_EVENT_GAMEPAD_AXIS, 0, BR_PAD_AXIS_LEFT_X, 0.2f);       /* (drift) */
        br_input_handle_event(&g_engine.input, &k);
        CHECK(g_engine.input.last_device == BR_DEVICE_MOUSE);
        k = pad_event(BR_EVENT_GAMEPAD_AXIS, 0, BR_PAD_AXIS_LEFT_X, 0.8f);
        br_input_handle_event(&g_engine.input, &k);
        CHECK(g_engine.input.last_device == BR_DEVICE_GAMEPAD);
        g_engine.input.last_device = BR_DEVICE_NONE;
    }
    {
        br_gamepad_raw raw;
        memset(&raw, 0, sizeof(raw));
        br_headless_set_gamepad(0, &raw);
    }
    teardown();
}

/* Tags in scripts: given, tested, listed, found (all of several), taken off;
 * a tag that isn't one is a mistake. */
static void test_script_tags(void) {
    br_entity runner, a, b;
    char text[64];
    setup();
    a = br_entity_create(&W);
    b = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, a, ID.name, NULL))->text, 32, "Bat");
    snprintf(((br_name *)br_add(&W, b, ID.name, NULL))->text, 32, "Slime");
    write_script("tagger.bs",
        "var found = 0\nvar both = 0\nvar first = \"\"\nvar listed = \"\"\nvar had = false\nvar gone = 0\nvar none = 0\n"
        "func start() {\n"
        "    add_tag(find(\"Bat\"), \"enemy\")\n"
        "    add_tag(find(\"Bat\"), \"flying\")\n"
        "    add_tag(find(\"Slime\"), \"enemy\")\n"
        "    found = len(find_tagged(\"enemy\"))\n"
        "    both = len(find_tagged(\"enemy\", \"flying\"))\n"
        "    first = first_tagged(\"flying\").name\n"
        "    for t in tags(find(\"Bat\")) { listed += t + \";\" }\n"
        "    had = remove_tag(find(\"Bat\"), \"flying\") and not has_tag(find(\"Bat\"), \"flying\")\n"
        "    gone = len(find_tagged(\"flying\"))\n"
        "    if first_tagged(\"boss\") == nil { none = 1 }\n"
        "}\n");
    runner = scripted("Runner", "tagger.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(runner, "found") == 2 && num(runner, "both") == 1 && num(runner, "gone") == 0 && num(runner, "none") == 1);
    CHECK(br_script_get_string(&W, runner, "first", text, sizeof(text)) && strcmp(text, "Bat") == 0);
    CHECK(br_script_get_string(&W, runner, "listed", text, sizeof(text)) && strcmp(text, "enemy;flying;") == 0);
    CHECK(br_tag_has(&W, a, "enemy") && !br_tag_has(&W, a, "flying") && br_tag_has(&W, b, "enemy"));
    write_script("tagger_bad.bs", "func start() {\n    add_tag(self, \"two words\")\n}\n");
    scripted("Bad", "tagger_bad.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK(br_script_error_count(&W) == 1 && strstr(br_script_last_error(&W), "isn't a tag") != NULL);
    CHECK(!br_script_check("func f() {\n    var e: entity = find_tagged(\"x\")\n}\n", "x.bs", text, sizeof(text)));   /* (a list) */
    teardown();
}

typedef struct counter_state { int starts, updates, hits; } counter_state;
static int g_destroyed;
static void cb_start(br_world *w, br_entity e, void *s) { BR_UNUSED(w); BR_UNUSED(e); ((counter_state *)s)->starts++; }
static void cb_update(br_world *w, br_entity e, void *s, float dt) { BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(dt); ((counter_state *)s)->updates++; }
static void cb_destroy(br_world *w, br_entity e, void *s) { BR_UNUSED(w); BR_UNUSED(e); g_destroyed += ((counter_state *)s)->updates; }
static const br_behaviour k_counter = { sizeof(counter_state), cb_start, cb_update, NULL, NULL, cb_destroy };

static void test_c_behaviour_and_reload(void) {
    br_entity c, s1, s2;
    setup();
    CHECK(br_script_register_behaviour("counter", &k_counter));
    c = scripted("c", "counter", NULL, br_v2(0, 0));
    g_destroyed = 0;
    steps(3);
    br_entity_destroy(&W, c);
    CHECK_EQ_INT(g_destroyed, 3);
    write_script("v.bs", "var version = 1\n");
    s1 = scripted("s1", "v.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK(num(s1, "version") == 1);
    write_script("v.bs", "var version = 2\n");
    br_script_reload(&W);                                  /* (file times have 1 s resolution) */
    s2 = scripted("s2", "v.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK(num(s1, "version") == 1 && num(s2, "version") == 2);   /* running ones keep their version */
    {   /* an object outlives the script that made it: its class stays when that file is read again */
        br_entity holder, maker;
        write_script("holder.bs", "var obj = nil\nvar got = 0\nfunc update(dt) {\n  if obj != nil { got = obj.get() }\n}\n");
        write_script("maker.bs", "class Thing {\n  var n = 5\n  func get() { return this.n * 2 }\n}\nfunc start() {\n  find(\"holder\").obj = Thing()\n}\n");
        holder = scripted("holder", "holder.bs", NULL, br_v2(0, 0));
        maker = scripted("maker", "maker.bs", NULL, br_v2(0, 0));
        steps(2);
        br_entity_destroy(&W, maker);
        write_script("maker.bs", "class Thing {\n  var n = 1\n  func get() { return this.n }\n}\nfunc start() { }\n");
        br_script_reload(&W);
        scripted("maker2", "maker.bs", NULL, br_v2(0, 0));
        steps(2);
        CHECK_EQ_INT(br_script_error_count(&W), 0);
        CHECK(num(holder, "got") == 10);
    }
    teardown();
}

static void test_garbage(void) {
    br_entity e;
    setup();
    write_script("churn.bs",
        "var keep = []\nvar n = 0\n"
        "func update(dt) {\n"
        "    var junk = []\n"
        "    for i in 200 { push(junk, \"text \" + i) }\n"
        "    n += 1\n"
        "    if n % 10 == 0 { push(keep, junk[5]) }\n"
        "}\n");
    e = scripted("churn", "churn.bs", NULL, br_v2(0, 0));
    steps(300);                                            /* ~60000 strings, mostly garbage */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    CHECK(num(e, "n") == 300);
    {
        double kept = 0;
        CHECK(br_script_call(&W, e, "update", (const double[]){ 0 }, 1));
        CHECK(br_script_get_number(&W, e, "n", &kept) && kept == 301);
    }
    teardown();                                            /* the leak check covers the heap */
}

/* The example project plays: walk right, collect the first coin, jump onto a platform. */
static void test_platformer_example(void) {
    int n, i;
    char err[200];
    br_entity hud = BR_ENTITY_NULL, player = BR_ENTITY_NULL;
    double coins = -1;
    const br_transform *pt;
    setup();
    br_assets_set_root("examples/platformer/");
    n = br_world_load_file(&W, "examples/platformer/main.scene", NULL, 0, err, sizeof(err));
    CHECK(n > 10);
    for (i = 0; i < (int)W.pools[ID.name].count; ++i) {
        const char *name = ((const br_name *)(W.pools[ID.name].data + (size_t)i * W.pools[ID.name].stride))->text;
        if (strcmp(name, "HUD") == 0) hud = W.pools[ID.name].entities[i];
        if (strcmp(name, "Player") == 0) player = W.pools[ID.name].entities[i];
    }
    steps(30);                                             /* settle */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(br_script_get_number(&W, hud, "total", &coins) && coins == 5);
    g_engine.input.keys[BR_KEY_RIGHT] = true;              /* run right to the first coin (x 120) */
    steps(70);
    g_engine.input.keys[BR_KEY_RIGHT] = false;
    CHECK(br_script_get_number(&W, hud, "coins", &coins) && coins == 1);
    {
        char text[64];
        const br_label *l = (const br_label *)br_get(&W, hud, ID.label);
        CHECK(l && strcmp(l->text, "Coins: 1 / 5") == 0);
        BR_UNUSED(text);
    }
    pt = (const br_transform *)br_get(&W, player, ID.transform);
    CHECK(pt && pt->position.x > 120 && br_absf(pt->position.y - 155.5f) < 1.5f);   /* on the ground */
    g_engine.input.keys[BR_KEY_LEFT] = true;               /* back under platform 1, then jump onto it */
    steps(25);
    g_engine.input.keys[BR_KEY_LEFT] = false;
    g_engine.input.keys_pressed[BR_KEY_SPACE] = true;
    steps(1);
    g_engine.input.keys_pressed[BR_KEY_SPACE] = false;
    steps(4);
    pt = (const br_transform *)br_get(&W, player, ID.transform);
    CHECK(pt && pt->position.y < 145);                     /* in the air */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    teardown();
}

static void test_tree_from_scripts(void) {
    br_entity boss, arm, hand;
    double v;
    setup();
    write_script("boss.bs",
        "var arms = 0\nvar hand_x = 0\nvar spawned = 0\n"
        "func start() {\n"
        "    var arm = child(self, \"Arm\")\n"
        "    arms = len(children(self))\n"
        "    hand_x = child(arm, \"Hand\").global_position.x\n"
        "    self.rotation = 90\n"
        "    var shot = spawn(\"part.scene\", vec2(5, 0), arm)\n"
        "    if shot.parent == arm and shot.global_position.y > 0 { spawned = 1 }\n"
        "    arm.parent = nil\n"
        "}\n");
    {
        br_world tmp;
        br_entity e;
        br_world_init(&tmp);
        br_builtin_register(&tmp, NULL);
        e = br_entity_create(&tmp);
        br_add(&tmp, e, br_component_find(&tmp, "Transform"), NULL);
        CHECK(br_world_save_file(&tmp, &e, 1, test_path("part.scene")));
        br_world_destroy(&tmp);
    }
    boss = scripted("Boss", "boss.bs", NULL, br_v2(100, 100));
    arm = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, arm, ID.name, NULL))->text, 32, "Arm");
    ((br_transform *)br_add(&W, arm, ID.transform, NULL))->position = br_v2(20, 0);
    br_set_parent(&W, arm, boss, false);
    hand = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, hand, ID.name, NULL))->text, 32, "Hand");
    ((br_transform *)br_add(&W, hand, ID.transform, NULL))->position = br_v2(10, 0);
    br_set_parent(&W, hand, arm, false);
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(br_script_get_number(&W, boss, "arms", &v) && v == 1);
    CHECK(br_script_get_number(&W, boss, "hand_x", &v) && v == 130);
    CHECK(br_script_get_number(&W, boss, "spawned", &v) && v == 1);
    CHECK(br_parent_of(&W, arm) == BR_ENTITY_NULL);                       /* detached, but didn't move */
    CHECK_NEAR(br_world_transform(&W, hand).position.y, 130, 1e-3);
    teardown();
}

/* Is `word` in `text` as a whole word? */
static bool has_word(const char *text, const char *word) {
    size_t n = strlen(word);
    const char *p = text;
    while ((p = strstr(p, word)) != NULL) {
        bool before = p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '_');
        bool after = isalnum((unsigned char)p[n]) || p[n] == '_';
        if (!before && !after) return true;
        p += n;
    }
    return false;
}

/* The VS Code extension knows every built-in function the language has - with
 * the engine's numbers of arguments - and highlights it, and the reference
 * (docs/scripting.md) describes it. The names the extension offers inside
 * strings (keys, buttons, axes, buses, components) are ones the engine takes. */
static void test_extension_knows_builtins(void) {
    size_t size, gsize, dsize;
    char *json = (char *)br_file_read_all("tools/vscode/brontoscript/lib/builtins.json", &size);
    char *grammar = (char *)br_file_read_all("tools/vscode/brontoscript/syntaxes/brontoscript.tmLanguage.json", &gsize);
    char *docs = (char *)br_file_read_all("docs/scripting.md", &dsize);
    int i;
    CHECK(json != NULL && grammar != NULL && docs != NULL);
    if (!json || !grammar || !docs) { br_free(json); br_free(grammar); br_free(docs); return; }
    CHECK(br_script_builtin_count() > 40 && br_script_builtin_name(br_script_builtin_count()) == NULL);
    for (i = 0; i < br_script_builtin_count(); ++i) {
        const char *name = br_script_builtin_name(i), *entry, *p;
        char key[64];
        int lo = -1, hi = -1, min = -1, params = 0;
        CHECK(br_script_builtin_arity(i, &lo, &hi));
        snprintf(key, sizeof(key), "\"name\": \"%s\"", name);
        entry = strstr(json, key);
        if (!entry) printf("      builtins.json is missing %s\n", name);
        CHECK(entry != NULL);
        if (entry) {                                     /* its "params": [...] and "min": n */
            const char *end = strstr(entry + 1, "\"name\": ");
            p = strstr(entry, "\"params\": [");
            if (p && (!end || p < end)) {
                const char *close = strchr(p, ']');
                for (p += 11; p < close; ++p) params += *p == '"';
                params /= 2;
            }
            p = strstr(entry, "\"min\": ");
            if (p && (!end || p < end)) min = atoi(p + 7);
            if (min != lo || (hi < 16 && params != hi))
                printf("      builtins.json: %s takes %d to %d arguments, not %d to %d\n", name, lo, hi, min, params);
            CHECK(min == lo && (hi >= 16 || params == hi));
        }
        snprintf(key, sizeof(key), "|%s|", name);
        if (!strstr(grammar, key) && !(snprintf(key, sizeof(key), "(%s|", name), strstr(grammar, key)) &&
            !(snprintf(key, sizeof(key), "|%s)", name), strstr(grammar, key))) {
            printf("      the grammar doesn't highlight %s\n", name);
            CHECK(false);
        }
        if (!has_word(docs, name)) printf("      docs/scripting.md doesn't mention %s\n", name);
        CHECK(has_word(docs, name));
    }
    {   /* the names offered inside strings, tried on the engine */
        static const struct { const char *list, *call; } uses[] = {
            { "keys", "key_down(v)" }, { "buttons", "button_down(v)" }, { "axes", "axis(v)" }, { "sticks", "stick(v)" },
            { "buses", "volume(v)" }, { "components", "has(self, v)" },
        };
        char *src = (char *)br_alloc(64 * 1024);
        size_t k;
        br_entity e;
        setup();
        for (k = 0; k < BR_ARRAY_COUNT(uses); ++k) {
            char key[64];
            const char *p, *close;
            snprintf(key, sizeof(key), "\"%s\": [", uses[k].list);
            p = strstr(json, key);
            CHECK(p != NULL);
            if (!p) continue;
            p = strchr(p, '[');
            close = strchr(p, ']');
            snprintf(src, 64 * 1024, "var n = 0\nfunc update(dt) {\n    for v in %.*s {\n        var r = %s\n        n += 1\n    }\n}\n",
                     (int)(close - p + 1), p, uses[k].call);
            snprintf(key, sizeof(key), "names_%d.bs", (int)k);
            write_script(key, src);
            e = scripted("Names", key, NULL, br_v2(0, 0));
            steps(1);
            if (br_script_error_count(&W)) printf("      %s: %s\n", uses[k].list, br_script_last_error(&W));
            CHECK_EQ_INT(br_script_error_count(&W), 0);
            CHECK(num(e, "n") > 1);
            br_entity_destroy(&W, e);
            steps(1);
        }
        br_free(src);
        teardown();
    }
    br_free(json);
    br_free(grammar);
    br_free(docs);
}


/* Static typing: annotations are checked before a script runs. */
static void test_types_checked(void) {
    static const struct { const char *src, *err; } cases[] = {
        /* accepted */
        { "var speed: number = 90\nvar lives: int = 3\nvar name: string\nvar alive: bool = true\nvar at: vec2 = vec2(1, 2)\n"
          "var items: list[number] = [1, 2.5]\nvar target: entity\nvar anything: any = \"x\"\nvar count := 0\n"
          "func hit(amount: int, from: entity) -> bool {\n    lives -= amount\n    return lives <= 0\n}\n"
          "func update(dt: number) -> void {\n    speed += dt * 2\n    var d := distance(at, self.position)\n    count += floor(d)\n"
          "    for i: int in 3 { count += i }\n    for v in items { speed += v }\n    if hit(1, nil) { print(\"out\") }\n}\n", NULL },
        { "@strict\nvar a: int = 1\nfunc f(x: number) -> number {\n    var y := x * 2\n    return y\n}\n", NULL },
        { "func f() -> int {\n    if true { return 1 } else { return 2 }\n}\n", NULL },
        { "func start() -> void {\n    self.scale = 2\n    self.parent = nil\n    var e: entity = find(\"x\")\n}\n", NULL },
        /* rejected */
        { "var speed: number = \"fast\"\n", "t.bs:1: speed is a number, so it can't hold a string" },
        { "var lives: int = 2.5\n", "lives is an int, and this number may have a fraction" },
        { "var n := nil\n", "can't tell the type of n's value" },
        { "var c := 1\nfunc f() {\n    c = \"two\"\n}\n", "t.bs:3: c is an int, so it can't hold a string" },
        { "func f(a: vec2) {\n}\nfunc g() {\n    f(3)\n}\n", "t.bs:4: f()'s parameter 'a' is a vec2, so it can't hold an int" },
        { "func f() -> int {\n    return \"x\"\n}\n", "what f() returns is an int, so it can't hold a string" },
        { "func f() -> int {\n    if true { return 1 }\n}\n", "not every way through f() ends with a return" },
        { "func f() -> void {\n    return 3\n}\n", "returns nothing (-> void), so return can't have a value" },
        { "func f() {\n    var s: string = \"a\" - 1\n}\n", "can't use - on a string and an int" },
        { "func f() {\n    key_down(3)\n}\n", "key_down() wants a string for argument 1, not an int" },
        { "func f() {\n    destroy(nil)\n}\n", "destroy() wants an entity for argument 1, not a nil" },
        { "func f() {\n    var v := vec2(1, 2)\n    v.z = 1\n}\n", "a vec2 has x, y and length, not 'z'" },
        { "func f() {\n    var xs: list[int] = [1, 2]\n    push(xs, \"three\")\n}\n", "push(): the list holds int, not string" },
        { "func f() {\n    var n := 3\n    for x in \"abc\" { }\n}\n", "for ... in needs a list or a number, not a string" },
        { "func update(dt: string) {\n}\n", "the engine passes a number as update()'s parameter 'dt', not a string" },
        { "var x: flaot = 1\n", "unknown type 'flaot'" },
        { "func f() {\n    var s := print(\"x\")\n}\n", "returns nothing" },
        { "func f() {\n    self.id = 3\n}\n", "id can't be changed" },
        { "@strict\nvar a = 1\n", "t.bs:2: @strict: give 'a' a type" },
        { "@strict\nfunc f(x) -> void {\n}\n", "@strict: give the parameter 'x' a type" },
        { "@strict\nfunc f() {\n}\n", "@strict: say what f() returns" },
        { "@strict\nfunc f() -> void {\n    var y = 2\n}\n", "@strict: give 'y' a type" },
        { "var a = 1\n@strict\n", "@strict goes at the top of the file" },
        { "@tool\n", "unknown annotation '@tool'" },
    };
    size_t i;
    char err[256];
    for (i = 0; i < BR_ARRAY_COUNT(cases); ++i) {
        bool ok = br_script_check(cases[i].src, "t.bs", err, sizeof(err));
        if (cases[i].err == NULL) { CHECK(ok); if (!ok) fprintf(stderr, "      case %u: %s\n", (unsigned)i, err); }
        else {
            CHECK(!ok && strstr(err, cases[i].err));
            if (ok || !strstr(err, cases[i].err)) fprintf(stderr, "      case %u: got \"%s\"\n", (unsigned)i, ok ? "(accepted)" : err);
        }
    }
    br_script_set_strict(true);                              /* the project's setting: every script strict */
    CHECK(!br_script_check("var a = 1\n", "t.bs", err, sizeof(err)) && strstr(err, "@strict: give 'a' a type"));
    CHECK(br_script_check("var a: int = 1\n", "t.bs", err, sizeof(err)));
    br_script_set_strict(false);
    CHECK(br_script_check("var a = 1\n", "t.bs", err, sizeof(err)));
}

/* ... and again when it runs: values from untyped code, other scripts and C. */
static void test_types_at_run_time(void) {
    br_entity e, f;
    double v;
    char s[64];
    setup();
    write_script("typed.bs",
        "var hp: int\nvar label: string\nvar spot: vec2\nvar bag: list[int]\nvar who: entity\nvar loose\n"
        "var ok := false\n"
        "func start() -> void {\n"
        "    ok = hp == 0 and label == \"\" and spot.x == 0 and len(bag) == 0 and who == nil\n"
        "    push(bag, 1)\n"
        "}\n"
        "func set_hp(v) -> void { hp = v }\n"
        "func add(a: int, b: int) -> int { return a + b }\n"
        "func half(n) -> int { return n / 2 }\n"
        "func update(dt: number) -> void { }\n");
    write_script("poke.bs",
        "var which = 0\n"
        "func update(dt) {\n"
        "    var t = find(\"typed\")\n"
        "    if which == 1 { t.set_hp(2.5) }\n"
        "    if which == 2 { t.hp = \"lots\" }\n"
        "    if which == 3 { t.half(3) }\n"
        "    if which == 4 { var x: int = t.add(2, 3); t.hp = x }\n"
        "}\n");
    e = scripted("typed", "typed.bs", NULL, br_v2(0, 0));
    f = scripted("poke", "poke.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK(br_script_get_number(&W, e, "ok", &v) && v == 1);          /* the defaults */
    CHECK(br_script_get_string(&W, e, "label", s, sizeof(s)) && s[0] == 0);
    CHECK(!br_script_set_number(&W, e, "label", 3));                  /* a string stays a string */
    CHECK(!br_script_set_number(&W, e, "hp", 1.5) && br_script_set_number(&W, e, "hp", 4));
    br_script_set_number(&W, f, "which", 4);
    steps(1);
    CHECK(br_script_get_number(&W, e, "hp", &v) && v == 5);
    br_script_set_number(&W, f, "which", 1);
    QUIET(steps(1));
    CHECK(strstr(br_script_last_error(&W), "typed.bs") && strstr(br_script_last_error(&W), "hp is an int, and 2.5 isn't a whole number"));
    teardown();

    setup();
    write_script("typed.bs", "var hp: int\nfunc update(dt) { }\n");
    write_script("poke.bs", "func update(dt) {\n    find(\"typed\").hp = \"lots\"\n}\n");
    scripted("typed", "typed.bs", NULL, br_v2(0, 0));
    scripted("poke", "poke.bs", NULL, br_v2(0, 0));
    QUIET(steps(2));
    CHECK(strstr(br_script_last_error(&W), "poke.bs:2: hp is an int, so it can't hold a string"));
    teardown();

    setup();
    write_script("ret.bs", "func half(n) -> int { return n / 2 }\nfunc start() { var r = half(3) }\n");
    scripted("r", "ret.bs", NULL, br_v2(0, 0));
    QUIET(steps(1));
    CHECK(strstr(br_script_last_error(&W), "what half() returns is an int, and 1.5 isn't a whole number"));
    teardown();

    setup();                                                   /* Script.vars must fit too */
    write_script("vars.bs", "var speed: number = 1\nfunc update(dt) { }\n");
    scripted("v", "vars.bs", "speed = \"fast\"", br_v2(0, 0));
    QUIET(steps(1));
    CHECK(strstr(br_script_last_error(&W), "Script.vars sets 'speed', but it is a number"));
    teardown();
}

void suite_script(void) {
    RUN(test_extension_knows_builtins);
    RUN(test_tree_from_scripts);
    RUN(test_platformer_example);
    RUN(test_saved_data);
    RUN(test_volume_and_keys);
    RUN(test_pantryquest_example);
    RUN(test_pantryquest_shutter);
    RUN(test_pantryquest_world);
    RUN(test_pantryquest_bosses);
    RUN(test_pantryquest_plank_and_whisk);
    RUN(test_pantryquest_banquet_gate);
    RUN(test_pantryquest_banquet_puzzles);
    RUN(test_pantryquest_banquet_waves);
    RUN(test_pantryquest_baron);
    RUN(test_pantryquest_upgrades);
    RUN(test_pantryquest_hints);
    RUN(test_pantryquest_stages);
    RUN(test_pantryquest_eight_ways);
    RUN(test_pantryquest_pocket_pantry);
    RUN(test_pantryquest_gamepad);
    RUN(test_pantryquest_files_and_settings);
    RUN(test_check_and_errors);
    RUN(test_types_checked);
    RUN(test_types_at_run_time);
    RUN(test_language);
    RUN(test_classes);
    RUN(test_class_errors);
    RUN(test_class_runtime_checks);
    RUN(test_class_garbage);
    RUN(test_entities_and_vars);
    RUN(test_runtime_errors);
    RUN(test_physics_callbacks_and_messages);
    RUN(test_spawn_destroy_input);
    RUN(test_gamepad_input);
    RUN(test_script_tags);
    RUN(test_tiles_substr_big_spawn);
    RUN(test_spawn_then_set);
    RUN(test_c_behaviour_and_reload);
    RUN(test_garbage);
}
