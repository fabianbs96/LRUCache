#include "caching/lru_cache.hpp"
#include <iostream>

template <typename T> void printAll(const T &map) {
  map.forEach(
      [](auto x, auto y) { std::cout << "(" << x << " => " << y << ") "; });
  std::cout << "\n";
}

template <typename TCache> uint64_t fib(uint64_t n, TCache &cache) {
  // std::cout << "fib(" << n << ")\n";
  if (n < 2)
    return n;

  if (auto prev = cache.get(n))
    return *prev;

  auto ret = fib(n - 1, cache) + fib(n - 2, cache);
  cache.insert(n, ret);
  return ret;
}

uint64_t fibIt(uint64_t n) {
  if (n < 2)
    return n;

  uint64_t a = 1, b = 1;

  for (uint64_t i = 2; i < n; ++i) {
    auto c = b;
    b = a + b;
    a = c;
  }
  return b;
}

using namespace caching;

int main() {
  uint64_t N = 65;

  lru_cache<uint64_t, uint64_t> cache(10, 20);
  std::cout << fib(N, cache) << std::endl;
  std::cout << fibIt(N) << std::endl;

  /* lru_cache<int, double> cache(3, 3);

   cache.insert(3, 4.5);
   cache.insert(4, 4.3);
   cache.insert(6, 4.7);

   if (auto val = cache.get(4)) {
     std::cout << "Found at 4: " << *val << std::endl;
   } else {
     std::cout << "Not found anything at 4" << std::endl;
   }

   printAll(cache);

   cache.insert(7, 33.3333);

   if (auto val = cache.get(3)) {
     std::cout << "Found at 3: " << *val << std::endl;
   } else {
     std::cout << "Not found anything at 3" << std::endl;
   }

   if (auto val = cache.get(6)) {
     std::cout << "Found at 6: " << *val << std::endl;
   } else {
     std::cout << "Not found anything at 6" << std::endl;
   }

   printAll(cache);
   cache.insert(7, 6);

   if (auto val = cache.get(4)) {
     std::cout << "Found at 4: " << *val << std::endl;
   } else {
     std::cout << "Not found anything at 4" << std::endl;
   }

   if (auto val = cache.get(7)) {
     std::cout << "Found at 7: " << *val << std::endl;
   } else {
     std::cout << "Not found anything at 7" << std::endl;
   }
   printAll(cache);
   cache.insert(7, 6, true);

   if (auto val = cache.get(7)) {
     std::cout << "Found at 7: " << *val << std::endl;
   } else {
     std::cout << "Not found anything at 7" << std::endl;
   }
   printAll(cache);*/
}