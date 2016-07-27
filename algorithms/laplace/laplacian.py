import math
import random

class Laplacian(object):
  """ A generator of pseudorandom numbers from a Laplacian distribution.
  """
  def __init__(self, b):
    """
    Args:
      b {float} A positive scaling factor. The distribution will have
      mu = 0 and sigma = sqrt(2) * b.
    """
    if b <= 0:
      raise Exception("b must be positive: %f" % b)
    self.b = b
    self.rand = random.SystemRandom()

  def sample(self):
    """ Returns the next sample from the distribution.
    """
    # The PDF for the distribution we are sampling is:
    #
    #         { (1/2b) exp(x/b)   ; if x < 0
    # f(x) =  |
    #         { (1/2b) exp(-x/b)  ; if x >= 0
    #
    # The CDF is therefore:
    #
    #         {  (1/2)exp(x/b)     ; if x < 0
    # F(x) =  |
    #         {  1 - (1/2)exp(-x/b)  ; if x >= 0
    #
    # Now let u be a uniform random variable on (-1, 1)
    # and define Y by
    #       {   b ln(1 + u)  ; if u < 0
    #  Y =  |
    #       {  -b ln(1 - u)  ; if u > 0
    #
    # We claim that Y has the distribution we want to sample.
    # To see this we show that the CDF of Y is equal to F(X).
    #
    # Y < x
    # iff
    # u < 0 and b ln(1 + u) < x or u >=0 and -b ln(1 - u) > x
    # iff
    # u < 0 and  u < exp(x/b) - 1 or u >= 0 and  u < 1 - exp(-x/b)
    #
    # First consider the case x >= 0. Then Y < x iff
    # u < 0 or u >= 0 and  u < 1 - exp(-x/b)
    # So P(Y < x) = 1/2 + 1/2 [1 - exp(-x/b)] = 1 - (1/2)exp(-x/b) = F(x)
    #
    # Now consider the case x < 0. Then Y < x iff
    # u > -1 and  u < exp(x/b) - 1
    # So P(Y < x) = 1/2 (exp(x/b) - 1 - -1) = 1/2 exp(x/b) = F(x)
    u = self.rand.uniform(-1.0, 1.0)
    if u >= 0:
      return -1 * self.b * math.log(1.0 - u)
    else:
      return self.b * math.log(u + 1.0)

def main():
  # This main function is a manual test of the code in this file.
  # P(|Laplacian(1)| > 1) = 0.368
  # P(|Laplacian(1)| > 2) = 0.135
  # TODO(rudominer, mironov) Add more in-depth testing. In particular consider
  # the chi-squared test written by mironov@ in the RAPPOR Github repo.
  laplacian_1 = Laplacian(1)
  count1 = 0
  count2 = 0
  for i in xrange(10000):
    x = laplacian_1.sample()
    if abs(x) > 1:
      count1 = count1 + 1
    if abs(x) > 2:
      count2 = count2 + 1
  print float(count1) / 10000
  print float(count2) / 10000

  # P(|Laplacian(2)| > 1) = 0.607
  # P(|Laplacian(2)| > 2) = 0.368
  laplacian_2 = Laplacian(2)
  count1 = 0
  count2 = 0
  for i in xrange(10000):
    x = laplacian_2.sample()
    if abs(x) > 1:
      count1 = count1 + 1
    if abs(x) > 2:
      count2 = count2 + 1
  print float(count1) / 10000
  print float(count2) / 10000

if __name__ == '__main__':
  main()
