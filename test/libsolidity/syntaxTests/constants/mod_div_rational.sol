contract C {
    fixed a1 = 0.1 % -0.4271087646484375;
    fixed a2 = 0.1 % 0.4271087646484375;
    fixed a3 = 0 / 0.123;
    fixed a4 = 0 / -0.123;
}
// ====
// compileViaYul: true
// ----
// UnimplementedFeatureError 1834: (28-53): Fixed point types not implemented.
