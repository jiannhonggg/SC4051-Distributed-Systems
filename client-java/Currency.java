public enum Currency {
    USD(1), JPY(2), SGD(3);

    private final int curr_const;

    Currency(int curr_const){
        this.curr_const = curr_const;
    }

    public int getCurrencyNumber() {
        return this.curr_const;
    }

    public static Currency fromInt(int x) {
        for (Currency c: Currency.values())
            if (c.curr_const == x)
                return c;
        return null;
    }
}